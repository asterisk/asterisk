import type {
  AsteriskIdentity,
  CapabilityResult,
  TargetProfile,
} from "./contracts.js";
import { unavailable, type LiveAsteriskDiscovery } from "./discovery.js";
import type { CommandResult } from "./executor.js";

export type AsteriskProbeOperation =
  | "identity"
  | "settings"
  | "cliCommands";

export interface AsteriskCommandGateway {
  execute(
    target: TargetProfile,
    operation: AsteriskProbeOperation,
    signal?: AbortSignal,
  ): Promise<CommandResult>;
}

export interface AmiDiscoveryClient {
  discover(
    target: TargetProfile,
    signal?: AbortSignal,
  ): Promise<{ version?: string; actions: ReadonlyArray<string> }>;
}

export interface AriDiscoveryClient {
  discover(
    target: TargetProfile,
    signal?: AbortSignal,
  ): Promise<{ version?: string; resources: ReadonlyArray<string> }>;
}

export class AllowlistedAsteriskDiscovery implements LiveAsteriskDiscovery {
  readonly gateway: AsteriskCommandGateway;
  readonly ami?: AmiDiscoveryClient;
  readonly ari?: AriDiscoveryClient;
  readonly now: () => Date;

  constructor(
    gateway: AsteriskCommandGateway,
    options: { ami?: AmiDiscoveryClient; ari?: AriDiscoveryClient; now?: () => Date } = {},
  ) {
    this.gateway = gateway;
    this.ami = options.ami;
    this.ari = options.ari;
    this.now = options.now ?? (() => new Date());
  }

  async discoverIdentity(
    target: TargetProfile,
    signal?: AbortSignal,
  ): Promise<CapabilityResult<AsteriskIdentity>> {
    const result = await this.gateway.execute(target, "identity", signal);
    if (result.status !== "succeeded") return this.#unavailable("Asterisk CLI identity probe", result);
    const version = /Asterisk\s+([A-Za-z0-9._~+-]+)/u.exec(result.stdout)?.[1];
    if (!version) return unavailable("Asterisk version was not present in CLI output", this.#time());
    return { state: "available", observedAt: this.#time(), value: { version } };
  }

  async discoverCli(
    target: TargetProfile,
    signal?: AbortSignal,
  ): Promise<CapabilityResult<ReadonlyArray<string>>> {
    const result = await this.gateway.execute(target, "cliCommands", signal);
    if (result.status !== "succeeded") return this.#unavailable("Asterisk CLI command discovery", result);
    const commands = result.stdout
      .split(/\r?\n/u)
      .map((line) => line.trim())
      .filter((line) => /^[a-z][a-z0-9_-]*(?:\s+[a-z][a-z0-9_-]*)+/iu.test(line))
      .map((line) => line.split(/\s{2,}|\t/u)[0]?.trim() ?? "")
      .filter(Boolean);
    return { state: "available", observedAt: this.#time(), value: [...new Set(commands)].sort() };
  }

  async discoverAmi(
    target: TargetProfile,
    signal?: AbortSignal,
  ): Promise<CapabilityResult<{ version?: string; actions: ReadonlyArray<string> }>> {
    if (!this.ami) return unavailable("AMI discovery client is not configured", this.#time());
    try {
      const value = await this.ami.discover(target, signal);
      return { state: "available", observedAt: this.#time(), value: normalizeNamed(value, "actions") };
    } catch (error) {
      return unavailable(`AMI discovery unavailable: ${safeReason(error)}`, this.#time());
    }
  }

  async discoverAri(
    target: TargetProfile,
    signal?: AbortSignal,
  ): Promise<CapabilityResult<{ version?: string; resources: ReadonlyArray<string> }>> {
    if (!this.ari) return unavailable("ARI discovery client is not configured", this.#time());
    try {
      const value = await this.ari.discover(target, signal);
      return { state: "available", observedAt: this.#time(), value: normalizeNamed(value, "resources") };
    } catch (error) {
      return unavailable(`ARI discovery unavailable: ${safeReason(error)}`, this.#time());
    }
  }

  #time(): string { return this.now().toISOString(); }

  #unavailable<T>(operation: string, result: CommandResult): CapabilityResult<T> {
    return unavailable(`${operation} ${result.status}${result.stderr ? `: ${result.stderr}` : ""}`, this.#time());
  }
}

function normalizeNamed<T extends "actions" | "resources">(
  value: { version?: string } & Record<T, ReadonlyArray<string>>,
  key: T,
): { version?: string } & Record<T, ReadonlyArray<string>> {
  return { ...value, [key]: [...new Set(value[key].map((entry) => entry.trim()).filter(Boolean))].sort() };
}

function safeReason(error: unknown): string {
  if (!(error instanceof Error)) return "unknown refusal";
  return error.message.replace(/\b(?:password|secret|token)\s*[=:]\s*[^\s,;]+/giu, "[REDACTED]");
}
