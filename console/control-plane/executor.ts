import { spawn } from "node:child_process";
import { assertNoSecretArguments, redactText } from "./redaction.js";

export type ExecutionStatus = "succeeded" | "failed" | "cancelled" | "timedOut";

export interface CommandRequest {
  executable: string;
  args: ReadonlyArray<string>;
  cwd?: string;
  input?: string;
  environment?: Readonly<Record<string, string>>;
  timeoutMs?: number;
  maxOutputBytes?: number;
  signal?: AbortSignal;
  redactedValues?: ReadonlyArray<string>;
}

export interface CommandResult {
  status: ExecutionStatus;
  exitCode: number | null;
  stdout: string;
  stderr: string;
  durationMs: number;
}

export interface ProcessExecutor {
  execute(request: CommandRequest): Promise<CommandResult>;
}

export interface NodeProcessExecutorOptions {
  allowedExecutables: ReadonlyArray<string>;
  defaultTimeoutMs?: number;
  defaultMaxOutputBytes?: number;
}

export class NodeProcessExecutor implements ProcessExecutor {
  readonly #allowed: ReadonlySet<string>;
  readonly #defaultTimeoutMs: number;
  readonly #defaultMaxOutputBytes: number;

  constructor(options: NodeProcessExecutorOptions) {
    this.#allowed = new Set(options.allowedExecutables.map(normalizeExecutable));
    this.#defaultTimeoutMs = positive(options.defaultTimeoutMs ?? 30_000, "defaultTimeoutMs");
    this.#defaultMaxOutputBytes = positive(
      options.defaultMaxOutputBytes ?? 1_048_576,
      "defaultMaxOutputBytes",
    );
  }

  async execute(request: CommandRequest): Promise<CommandResult> {
    if (!this.#allowed.has(normalizeExecutable(request.executable))) {
      throw new Error(`Executable is not allowlisted: ${request.executable}`);
    }
    assertNoSecretArguments(request.args);
    const timeoutMs = positive(request.timeoutMs ?? this.#defaultTimeoutMs, "timeoutMs");
    const maxOutputBytes = positive(
      request.maxOutputBytes ?? this.#defaultMaxOutputBytes,
      "maxOutputBytes",
    );
    if (request.signal?.aborted) return emptyResult("cancelled");

    const started = Date.now();
    return await new Promise<CommandResult>((resolve, reject) => {
      const child = spawn(request.executable, [...request.args], {
        cwd: request.cwd,
        env: request.environment ? { ...process.env, ...request.environment } : process.env,
        shell: false,
        windowsHide: true,
        stdio: ["pipe", "pipe", "pipe"],
      });
      let stdout: Buffer<ArrayBufferLike> = Buffer.alloc(0);
      let stderr: Buffer<ArrayBufferLike> = Buffer.alloc(0);
      let terminalStatus: ExecutionStatus | undefined;
      let settled = false;

      const terminate = (status: ExecutionStatus): void => {
        if (terminalStatus) return;
        terminalStatus = status;
        child.kill("SIGTERM");
        setTimeout(() => child.kill("SIGKILL"), 500).unref();
      };
      const onAbort = (): void => terminate("cancelled");
      request.signal?.addEventListener("abort", onAbort, { once: true });
      const timer = setTimeout(() => terminate("timedOut"), timeoutMs);

      const collect = (
        current: Buffer<ArrayBufferLike>,
        chunk: Buffer<ArrayBufferLike>,
      ): Buffer<ArrayBufferLike> => {
        const combined = Buffer.concat([current, chunk]);
        if (combined.byteLength > maxOutputBytes) {
          terminate("failed");
          return combined.subarray(0, maxOutputBytes);
        }
        return combined;
      };
      child.stdout.on("data", (chunk: Buffer) => { stdout = collect(stdout, chunk); });
      child.stderr.on("data", (chunk: Buffer) => { stderr = collect(stderr, chunk); });
      child.once("error", (error) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        request.signal?.removeEventListener("abort", onAbort);
        reject(error);
      });
      child.once("close", (exitCode) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        request.signal?.removeEventListener("abort", onAbort);
        const status = terminalStatus ?? (exitCode === 0 ? "succeeded" : "failed");
        const redaction = { extraValues: request.redactedValues };
        resolve({
          status,
          exitCode,
          stdout: redactText(stdout.toString("utf8"), redaction),
          stderr: redactText(stderr.toString("utf8"), redaction),
          durationMs: Date.now() - started,
        });
      });
      if (request.input === undefined) child.stdin.end();
      else child.stdin.end(request.input);
    });
  }
}

function normalizeExecutable(value: string): string {
  return value.trim().replaceAll("\\", "/").toLowerCase();
}

function positive(value: number, name: string): number {
  if (!Number.isSafeInteger(value) || value <= 0) throw new Error(`${name} must be a positive integer`);
  return value;
}

function emptyResult(status: ExecutionStatus): CommandResult {
  return { status, exitCode: null, stdout: "", stderr: "", durationMs: 0 };
}
