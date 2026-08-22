import type { CommandResult, ProcessExecutor } from "./executor.js";

export interface SshTarget {
  host: string;
  port: number;
  user: string;
  knownHostsPath: string;
}

export interface ApprovedSshIdentity {
  host: string;
  port: number;
}

export type RemoteProbe = "osRelease" | "asteriskVersion" | "freeStorage" | "privilege";

const REMOTE_PROBES: Readonly<Record<RemoteProbe, ReadonlyArray<string>>> = {
  osRelease: ["cat", "/etc/os-release"],
  asteriskVersion: ["asterisk", "-rx", "core show version"],
  freeStorage: ["df", "-B1", "--output=avail", "/"],
  privilege: ["sudo", "-n", "true"],
};

export class HostKeyMismatchError extends Error {
  readonly host: string;
  readonly port: number;

  constructor(host: string, port: number) {
    super(`SSH host key mismatch for ${host}:${port}`);
    this.name = "HostKeyMismatchError";
    this.host = host;
    this.port = port;
  }
}

export class SshPolicyAdapter {
  readonly executor: ProcessExecutor;
  readonly approvedIdentities: ReadonlySet<string>;

  constructor(executor: ProcessExecutor, approvedIdentities: ReadonlyArray<ApprovedSshIdentity>) {
    this.executor = executor;
    this.approvedIdentities = new Set(approvedIdentities.map(({ host, port }) => identity(host, port)));
  }

  buildArguments(target: SshTarget, probe: RemoteProbe): ReadonlyArray<string> {
    validateTarget(target);
    if (!this.approvedIdentities.has(identity(target.host, target.port))) {
      throw new Error(`SSH target is absent from the approved exact host/port inventory: ${target.host}:${target.port}`);
    }
    return [
      "-T",
      "-p", String(target.port),
      "-o", "BatchMode=yes",
      "-o", "StrictHostKeyChecking=accept-new",
      "-o", "UpdateHostKeys=no",
      "-o", `UserKnownHostsFile=${target.knownHostsPath}`,
      "-o", "ConnectTimeout=10",
      `${target.user}@${target.host}`,
      ...REMOTE_PROBES[probe],
    ];
  }

  async runProbe(target: SshTarget, probe: RemoteProbe, signal?: AbortSignal): Promise<CommandResult> {
    const result = await this.executor.execute({
      executable: "ssh",
      args: this.buildArguments(target, probe),
      timeoutMs: 20_000,
      maxOutputBytes: 256 * 1024,
      signal,
    });
    if (/REMOTE HOST IDENTIFICATION HAS CHANGED|Host key verification failed/iu.test(result.stderr)) {
      throw new HostKeyMismatchError(target.host, target.port);
    }
    return result;
  }
}

function identity(host: string, port: number): string {
  return `${host.toLowerCase()}:${port}`;
}

function validateTarget(target: SshTarget): void {
  if (!/^([a-zA-Z0-9](?:[a-zA-Z0-9.-]{0,251}[a-zA-Z0-9])?|\[[0-9a-fA-F:]+\])$/u.test(target.host)) {
    throw new Error("SSH host must be an exact DNS name, IPv4 address, or bracketed IPv6 address");
  }
  if (!Number.isSafeInteger(target.port) || target.port < 1 || target.port > 65_535) {
    throw new Error("SSH port is invalid");
  }
  if (!/^[a-z_][a-z0-9_-]{0,31}$/u.test(target.user)) throw new Error("SSH user is invalid");
  if (!/^(?:[a-zA-Z]:[\\/]|\/).+/u.test(target.knownHostsPath)) {
    throw new Error("known_hosts must use a persistent absolute path");
  }
  if (/null|nul$|dev[\\/]null/iu.test(target.knownHostsPath)) {
    throw new Error("Null or ephemeral known_hosts stores are prohibited");
  }
}
