import type { ApplyResult, ChangePlan, StructuredDiff } from "./contracts.js";

export interface ConfigDocument {
  resource: string;
  value: unknown;
}

export interface ConfigTransport {
  read(resource: string, signal?: AbortSignal): Promise<unknown>;
  backup(resource: string, signal?: AbortSignal): Promise<string>;
  stage(resource: string, value: unknown, signal?: AbortSignal): Promise<string>;
  validate(stagedHandle: string, signal?: AbortSignal): Promise<void>;
  apply(stagedHandle: string, signal?: AbortSignal): Promise<void>;
  rollback(backupHandle: string, signal?: AbortSignal): Promise<void>;
}

export class StructuredConfigPlanner {
  readonly now: () => Date;

  constructor(now = () => new Date()) {
    this.now = now;
  }

  async createPlan(
    id: string,
    targetId: string,
    desired: ReadonlyArray<ConfigDocument>,
    transport: Pick<ConfigTransport, "read">,
    signal?: AbortSignal,
  ): Promise<ChangePlan> {
    ensureUniqueResources(desired);
    const diffs: StructuredDiff[] = [];
    for (const document of desired) {
      throwIfAborted(signal);
      const before = await transport.read(document.resource, signal);
      const changedPaths = diffPaths(before, document.value);
      if (changedPaths.length > 0) diffs.push({ resource: document.resource, before, after: document.value, changedPaths });
    }
    const actions = diffs.flatMap((diff) => [
      { id: `backup:${diff.resource}`, kind: "backup" as const, description: `Back up ${diff.resource}`, resource: diff.resource },
      { id: `stage:${diff.resource}`, kind: "stage" as const, description: `Stage ${diff.resource}`, resource: diff.resource },
      { id: `validate:${diff.resource}`, kind: "validate" as const, description: `Validate ${diff.resource}`, resource: diff.resource },
      { id: `apply:${diff.resource}`, kind: "apply" as const, description: `Apply ${diff.resource}`, resource: diff.resource },
      { id: `post-read:${diff.resource}`, kind: "postRead" as const, description: `Verify ${diff.resource}`, resource: diff.resource },
    ]);
    return {
      id,
      targetId,
      createdAt: this.now().toISOString(),
      summary: diffs.length === 0 ? "No configuration changes" : `Change ${diffs.length} configuration resource(s)`,
      actions,
      diffs,
      requiredStorageBytes: Buffer.byteLength(JSON.stringify(diffs), "utf8") * 3,
      destructive: false,
    };
  }
}

export class ConfigTransaction {
  readonly transport: ConfigTransport;
  readonly now: () => Date;

  constructor(transport: ConfigTransport, now = () => new Date()) {
    this.transport = transport;
    this.now = now;
  }

  async apply(plan: ChangePlan, signal?: AbortSignal): Promise<ApplyResult> {
    const startedAt = this.now().toISOString();
    const completed: string[] = [];
    const applied: Array<{ resource: string; backup: string }> = [];
    let failedAction: string | undefined;
    try {
      for (const diff of plan.diffs) {
        throwIfAborted(signal);
        failedAction = `backup:${diff.resource}`;
        const backup = await this.transport.backup(diff.resource, signal);
        completed.push(failedAction);
        failedAction = `stage:${diff.resource}`;
        const staged = await this.transport.stage(diff.resource, diff.after, signal);
        completed.push(failedAction);
        failedAction = `validate:${diff.resource}`;
        await this.transport.validate(staged, signal);
        completed.push(failedAction);
        failedAction = `apply:${diff.resource}`;
        await this.transport.apply(staged, signal);
        applied.push({ resource: diff.resource, backup });
        completed.push(failedAction);
        failedAction = `post-read:${diff.resource}`;
        const actual = await this.transport.read(diff.resource, signal);
        if (!equal(actual, diff.after)) throw new Error(`Post-read mismatch for ${diff.resource}`);
        completed.push(failedAction);
      }
      return result(plan.id, "applied", startedAt, this.now(), completed, false, "Configuration applied and verified");
    } catch (error) {
      let rollbackSucceeded = true;
      for (const entry of [...applied].reverse()) {
        try {
          await this.transport.rollback(entry.backup);
          completed.push(`rollback:${entry.resource}`);
        } catch {
          rollbackSucceeded = false;
        }
      }
      const cancelled = signal?.aborted === true || isAbortError(error);
      const status = applied.length > 0 && rollbackSucceeded ? "rolledBack" : cancelled ? "cancelled" : "failed";
      return {
        planId: plan.id,
        status,
        startedAt,
        finishedAt: this.now().toISOString(),
        completedActions: completed,
        failedAction,
        rollbackAttempted: applied.length > 0,
        rollbackSucceeded: applied.length > 0 ? rollbackSucceeded : undefined,
        message: error instanceof Error ? error.message : "Configuration transaction failed",
      };
    }
  }
}

export function diffPaths(before: unknown, after: unknown, path = "$"): ReadonlyArray<string> {
  if (equal(before, after)) return [];
  if (!isRecord(before) || !isRecord(after)) return [path];
  const keys = [...new Set([...Object.keys(before), ...Object.keys(after)])].sort();
  return keys.flatMap((key) => diffPaths(before[key], after[key], `${path}.${key}`));
}

function ensureUniqueResources(documents: ReadonlyArray<ConfigDocument>): void {
  const resources = new Set<string>();
  for (const document of documents) {
    if (!/^\/[a-zA-Z0-9._/-]+$/u.test(document.resource)) throw new Error(`Invalid config resource: ${document.resource}`);
    if (resources.has(document.resource)) throw new Error(`Duplicate config resource: ${document.resource}`);
    resources.add(document.resource);
  }
}

function throwIfAborted(signal?: AbortSignal): void {
  if (signal?.aborted) throw new DOMException("Operation cancelled", "AbortError");
}

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === "AbortError";
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function equal(left: unknown, right: unknown): boolean {
  return JSON.stringify(canonical(left)) === JSON.stringify(canonical(right));
}

function canonical(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(canonical);
  if (isRecord(value)) return Object.fromEntries(Object.keys(value).sort().map((key) => [key, canonical(value[key])]));
  return value;
}

function result(
  planId: string,
  status: ApplyResult["status"],
  startedAt: string,
  finished: Date,
  completedActions: ReadonlyArray<string>,
  rollbackAttempted: boolean,
  message: string,
): ApplyResult {
  return { planId, status, startedAt, finishedAt: finished.toISOString(), completedActions, rollbackAttempted, message };
}
