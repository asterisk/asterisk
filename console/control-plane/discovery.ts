import type {
  AsteriskIdentity,
  CapabilityResult,
  CapabilitySnapshot,
  OperatingSystemIdentity,
  TargetProfile,
} from "./contracts.js";
import type { CommandResult, ProcessExecutor } from "./executor.js";

export interface DockerTarget {
  id: string;
  name: string;
  project: string;
  service?: string;
  labels: Readonly<Record<string, string>>;
}

export const DING_DOCKER_PROJECT_LABEL = "io.ding.pbx.project";

export interface LiveAsteriskDiscovery {
  discoverCli(target: TargetProfile, signal?: AbortSignal): Promise<CapabilityResult<ReadonlyArray<string>>>;
  discoverAmi(target: TargetProfile, signal?: AbortSignal): Promise<CapabilityResult<{ version?: string; actions: ReadonlyArray<string> }>>;
  discoverAri(target: TargetProfile, signal?: AbortSignal): Promise<CapabilityResult<{ version?: string; resources: ReadonlyArray<string> }>>;
  discoverIdentity(target: TargetProfile, signal?: AbortSignal): Promise<CapabilityResult<AsteriskIdentity>>;
}

export class TargetDiscovery {
  readonly executor: ProcessExecutor;
  readonly now: () => Date;

  constructor(executor: ProcessExecutor, now = () => new Date()) {
    this.executor = executor;
    this.now = now;
  }

  async discoverWslDistributions(signal?: AbortSignal): Promise<ReadonlyArray<string>> {
    const result = await this.executor.execute({
      executable: "wsl.exe",
      args: ["--list", "--quiet"],
      signal,
      timeoutMs: 10_000,
      maxOutputBytes: 64 * 1024,
    });
    ensureSuccess(result, "WSL distribution discovery");
    return result.stdout
      .replaceAll("\0", "")
      .split(/\r?\n/u)
      .map((name) => name.trim())
      .filter((name) => name.length > 0)
      .filter((name) => !/^docker-desktop(?:-data)?$/iu.test(name));
  }

  async discoverLocalDocker(project: string, signal?: AbortSignal): Promise<ReadonlyArray<DockerTarget>> {
    validateDockerLabel(project, "project");
    const result = await this.executor.execute({
      executable: "docker",
      args: [
        "ps", "--all",
        "--filter", `label=${DING_DOCKER_PROJECT_LABEL}=${project}`,
        "--format", "{{json .}}",
      ],
      signal,
      timeoutMs: 15_000,
      maxOutputBytes: 512 * 1024,
    });
    ensureSuccess(result, "Local Docker discovery");
    return result.stdout.split(/\r?\n/u).filter(Boolean).map((line) => {
      const parsed = JSON.parse(line) as Record<string, string>;
      const labels = parseDockerLabels(parsed.Labels ?? "");
      if (labels[DING_DOCKER_PROJECT_LABEL] !== project) {
        throw new Error("Docker result lacks the required project ownership label");
      }
      return {
        id: parsed.ID ?? "",
        name: parsed.Names ?? "",
        project,
        service: labels["com.docker.compose.service"],
        labels,
      };
    });
  }

  parseDebianOperatingSystem(osRelease: string): CapabilityResult<OperatingSystemIdentity> {
    const observedAt = this.now().toISOString();
    const fields = Object.fromEntries(
      osRelease.split(/\r?\n/u).filter((line) => /^[A-Z_]+=/u.test(line)).map((line) => {
        const index = line.indexOf("=");
        return [line.slice(0, index), line.slice(index + 1).replace(/^['"]|['"]$/gu, "")];
      }),
    );
    const id = fields.ID?.toLowerCase();
    if (id !== "debian" && id !== "ubuntu") {
      return unavailable(`Unsupported Linux distribution: ${id || "unknown"}`, observedAt);
    }
    if (!fields.VERSION_ID) return unavailable("Operating-system version is unavailable", observedAt);
    return {
      state: "available",
      observedAt,
      value: { id, versionId: fields.VERSION_ID, prettyName: fields.PRETTY_NAME ?? id },
    };
  }
}

export async function buildCapabilitySnapshot(
  target: TargetProfile,
  live: LiveAsteriskDiscovery,
  operatingSystem: CapabilityResult<OperatingSystemIdentity>,
  freeStorageBytes: CapabilityResult<number>,
  canElevate: CapabilityResult<boolean>,
  now = () => new Date(),
  signal?: AbortSignal,
): Promise<CapabilitySnapshot> {
  const [asterisk, cli, ami, ari] = await Promise.all([
    live.discoverIdentity(target, signal),
    live.discoverCli(target, signal),
    live.discoverAmi(target, signal),
    live.discoverAri(target, signal),
  ]);
  return {
    targetId: target.id,
    observedAt: now().toISOString(),
    operatingSystem,
    asterisk,
    cli,
    ami,
    ari,
    freeStorageBytes,
    canElevate,
  };
}

export function unavailable<T>(reason: string, observedAt = new Date().toISOString()): CapabilityResult<T> {
  return { state: "unavailable", reason, observedAt };
}

function ensureSuccess(result: CommandResult, operation: string): void {
  if (result.status !== "succeeded") throw new Error(`${operation} ${result.status}: ${result.stderr}`);
}

function validateDockerLabel(value: string, label: string): void {
  if (!/^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$/u.test(value)) throw new Error(`Invalid Docker ${label}`);
}

function parseDockerLabels(value: string): Readonly<Record<string, string>> {
  return Object.fromEntries(value.split(",").filter(Boolean).map((pair) => {
    const index = pair.indexOf("=");
    return index < 0 ? [pair.trim(), ""] : [pair.slice(0, index).trim(), pair.slice(index + 1)];
  }));
}
