import assert from "node:assert/strict";
import test from "node:test";

import {
  ConfigTransaction,
  AllowlistedAsteriskDiscovery,
  HostKeyMismatchError,
  NodeProcessExecutor,
  SourceProvisioningPlanner,
  SshPolicyAdapter,
  StructuredConfigPlanner,
  TargetDiscovery,
  assertNoSecretArguments,
  buildCapabilitySnapshot,
  buildProvisioningRecipe,
  diffPaths,
  redactRecord,
  redactText,
  unavailable,
} from "../../control-plane/index.ts";
import type {
  CapabilityResult,
  ConfigTransport,
  ProcessExecutor,
  CommandRequest,
  CommandResult,
  LiveAsteriskDiscovery,
  TargetProfile,
} from "../../control-plane/index.ts";

const NOW = new Date("2026-08-22T12:00:00.000Z");
const now = () => NOW;

class FakeExecutor implements ProcessExecutor {
  readonly requests: CommandRequest[] = [];
  readonly results: CommandResult[];
  constructor(results: CommandResult[]) { this.results = results; }
  async execute(request: CommandRequest): Promise<CommandResult> {
    this.requests.push(request);
    const result = this.results.shift();
    if (!result) throw new Error("No fake result available");
    return result;
  }
}

function command(stdout = "", stderr = "", status: CommandResult["status"] = "succeeded"): CommandResult {
  return { status, exitCode: status === "succeeded" ? 0 : 1, stdout, stderr, durationMs: 1 };
}

test("redacts bearer credentials, configured values, and secret record fields", () => {
  assert.equal(redactText("Authorization: Bearer abc.def.ghi", { extraValues: ["abc.def.ghi"] }), "Authorization: [REDACTED]");
  assert.deepEqual(redactRecord({ username: "alice", password: "do-not-print" }), {
    username: "alice",
    password: "[REDACTED]",
  });
});

test("rejects secret-bearing command arguments", () => {
  assert.throws(() => assertNoSecretArguments(["--password=hunter2"]), /prohibited/u);
  assert.doesNotThrow(() => assertNoSecretArguments(["--version"]));
});

test("bounded executor refuses a non-allowlisted executable", async () => {
  const executor = new NodeProcessExecutor({ allowedExecutables: ["node"] });
  await assert.rejects(executor.execute({ executable: "powershell", args: ["-NoProfile"] }), /not allowlisted/u);
});

test("bounded executor captures output without a shell", async () => {
  const executor = new NodeProcessExecutor({ allowedExecutables: [process.execPath] });
  const result = await executor.execute({
    executable: process.execPath,
    args: ["-e", "process.stdout.write('bounded')"],
    timeoutMs: 2_000,
    maxOutputBytes: 1024,
  });
  assert.equal(result.status, "succeeded");
  assert.equal(result.stdout, "bounded");
});

test("bounded executor cancels a running child", async () => {
  const executor = new NodeProcessExecutor({ allowedExecutables: [process.execPath] });
  const controller = new AbortController();
  setTimeout(() => controller.abort(), 20);
  const result = await executor.execute({
    executable: process.execPath,
    args: ["-e", "setTimeout(() => {}, 5000)"],
    signal: controller.signal,
    timeoutMs: 2_000,
  });
  assert.equal(result.status, "cancelled");
});

test("bounded executor enforces a deadline", async () => {
  const executor = new NodeProcessExecutor({ allowedExecutables: [process.execPath] });
  const result = await executor.execute({
    executable: process.execPath,
    args: ["-e", "setTimeout(() => {}, 5000)"],
    timeoutMs: 20,
  });
  assert.equal(result.status, "timedOut");
});

test("bounded executor stops and truncates oversized output", async () => {
  const executor = new NodeProcessExecutor({ allowedExecutables: [process.execPath] });
  const result = await executor.execute({
    executable: process.execPath,
    args: ["-e", "process.stdout.write('x'.repeat(4096))"],
    timeoutMs: 2_000,
    maxOutputBytes: 32,
  });
  assert.equal(result.status, "failed");
  assert.equal(Buffer.byteLength(result.stdout), 32);
});

test("bounded executor redacts sensitive child output", async () => {
  const executor = new NodeProcessExecutor({ allowedExecutables: [process.execPath] });
  const result = await executor.execute({
    executable: process.execPath,
    args: ["-e", "process.stdout.write(process.env.TEST_REDACTION_VALUE ?? '')"],
    environment: { TEST_REDACTION_VALUE: "fixture-sensitive-value" },
    redactedValues: ["fixture-sensitive-value"],
  });
  assert.equal(result.stdout, "[REDACTED]");
});

test("WSL discovery excludes Docker-owned distributions", async () => {
  const fake = new FakeExecutor([command("Ubuntu\r\ndocker-desktop\r\nDebian\r\ndocker-desktop-data\r\n")]);
  const discovery = new TargetDiscovery(fake, now);
  assert.deepEqual(await discovery.discoverWslDistributions(), ["Ubuntu", "Debian"]);
  assert.deepEqual(fake.requests[0]?.args, ["--list", "--quiet"]);
});

test("Docker discovery requires and returns exact project ownership labels", async () => {
  const line = JSON.stringify({
    ID: "abc123",
    Names: "ding-asterisk-1",
    Labels: "io.ding.pbx.project=ding,com.docker.compose.project=ding,com.docker.compose.service=asterisk",
  });
  const fake = new FakeExecutor([command(`${line}\n`)]);
  const targets = await new TargetDiscovery(fake, now).discoverLocalDocker("ding");
  assert.equal(targets[0]?.project, "ding");
  assert.equal(targets[0]?.service, "asterisk");
  assert.ok(fake.requests[0]?.args.includes("label=io.ding.pbx.project=ding"));
});

test("Docker discovery rejects an ownership-label mismatch", async () => {
  const line = JSON.stringify({ ID: "abc123", Names: "other", Labels: "com.docker.compose.project=other" });
  const fake = new FakeExecutor([command(`${line}\n`)]);
  await assert.rejects(new TargetDiscovery(fake, now).discoverLocalDocker("ding"), /ownership label/u);
});

test("Debian and Ubuntu are detected while unsupported distributions are honest unavailable results", () => {
  const discovery = new TargetDiscovery(new FakeExecutor([]), now);
  assert.deepEqual(discovery.parseDebianOperatingSystem('ID=ubuntu\nVERSION_ID="24.04"\nPRETTY_NAME="Ubuntu 24.04 LTS"').value, {
    id: "ubuntu",
    versionId: "24.04",
    prettyName: "Ubuntu 24.04 LTS",
  });
  const unsupported = discovery.parseDebianOperatingSystem("ID=fedora\nVERSION_ID=42");
  assert.equal(unsupported.state, "unavailable");
  assert.match(unsupported.reason ?? "", /Unsupported/u);
});

test("SSH adapter emits scoped trust-on-first-use arguments and a persistent known_hosts path", () => {
  const adapter = new SshPolicyAdapter(new FakeExecutor([]), [{ host: "pbx.internal.example", port: 2222 }]);
  const args = adapter.buildArguments({
    host: "pbx.internal.example",
    port: 2222,
    user: "operator",
    knownHostsPath: "C:\\Users\\operator\\.ssh\\known_hosts",
  }, "osRelease");
  assert.ok(args.includes("StrictHostKeyChecking=accept-new"));
  assert.ok(args.includes("UpdateHostKeys=no"));
  assert.ok(args.includes("UserKnownHostsFile=C:\\Users\\operator\\.ssh\\known_hosts"));
  assert.deepEqual(args.slice(-2), ["cat", "/etc/os-release"]);
});

test("SSH adapter stops on a host-key mismatch", async () => {
  const fake = new FakeExecutor([command("", "REMOTE HOST IDENTIFICATION HAS CHANGED", "failed")]);
  const adapter = new SshPolicyAdapter(fake, [{ host: "pbx.internal.example", port: 22 }]);
  await assert.rejects(adapter.runProbe({
    host: "pbx.internal.example",
    port: 22,
    user: "operator",
    knownHostsPath: "/home/operator/.ssh/known_hosts",
  }, "privilege"), HostKeyMismatchError);
});

test("SSH adapter refuses wildcard hosts and null known_hosts stores", () => {
  const adapter = new SshPolicyAdapter(new FakeExecutor([]), [{ host: "pbx.internal.example", port: 22 }]);
  assert.throws(() => adapter.buildArguments({ host: "*", port: 22, user: "root", knownHostsPath: "/dev/null" }, "osRelease"));
});

test("SSH adapter refuses a valid but uninventoried host or port", () => {
  const adapter = new SshPolicyAdapter(new FakeExecutor([]), [{ host: "pbx.internal.example", port: 22 }]);
  assert.throws(() => adapter.buildArguments({
    host: "pbx.internal.example",
    port: 2222,
    user: "operator",
    knownHostsPath: "/home/operator/.ssh/known_hosts",
  }, "osRelease"), /approved exact host\/port inventory/u);
});

const provisioningRequest = {
  id: "provision-1",
  targetId: "target-1",
  operatingSystem: { id: "ubuntu" as const, versionId: "24.04", prettyName: "Ubuntu" },
  repository: "https://github.com/asterisk/asterisk.git",
  commit: "0123456789abcdef0123456789abcdef01234567",
  sourceDirectory: "/opt/src/asterisk",
  installPrefix: "/usr/local",
  requiredStorageBytes: 2_000_000_000,
};

test("source provisioning is pinned to an exact commit", () => {
  const recipe = buildProvisioningRecipe(provisioningRequest);
  assert.deepEqual(recipe[3]?.slice(-2), ["origin", provisioningRequest.commit]);
  assert.deepEqual(recipe[4]?.slice(-2), ["--detach", provisioningRequest.commit]);
});

test("source provisioning refuses abbreviated commits and non-HTTPS repositories", () => {
  assert.throws(() => buildProvisioningRecipe({ ...provisioningRequest, commit: "abc123" }), /full SHA-1/u);
  assert.throws(() => buildProvisioningRecipe({ ...provisioningRequest, repository: "git@example:repo.git" }), /HTTPS/u);
});

test("provisioning planner refuses low storage", () => {
  assert.throws(
    () => new SourceProvisioningPlanner(now).createPlan(provisioningRequest, 1_000, true),
    /Insufficient storage/u,
  );
});

test("provisioning planner refuses unavailable privilege elevation", () => {
  assert.throws(
    () => new SourceProvisioningPlanner(now).createPlan(provisioningRequest, 3_000_000_000, false),
    /privilege elevation/u,
  );
});

test("structured config diff reports exact nested paths", () => {
  assert.deepEqual(diffPaths({ sip: { port: 5060, tls: false } }, { sip: { port: 5061, tls: false } }), ["$.sip.port"]);
});

class MemoryTransport implements ConfigTransport {
  readonly values = new Map<string, unknown>();
  readonly backups = new Map<string, unknown>();
  readonly staged = new Map<string, { resource: string; value: unknown }>();
  failValidation = false;
  failRollback = false;
  corruptAfterApply = false;

  async read(resource: string): Promise<unknown> { return this.values.get(resource); }
  async backup(resource: string): Promise<string> {
    const handle = `backup:${resource}`;
    this.backups.set(handle, structuredClone(this.values.get(resource)));
    return handle;
  }
  async stage(resource: string, value: unknown): Promise<string> {
    const handle = `stage:${resource}`;
    this.staged.set(handle, { resource, value: structuredClone(value) });
    return handle;
  }
  async validate(): Promise<void> { if (this.failValidation) throw new Error("validation refused"); }
  async apply(stagedHandle: string): Promise<void> {
    const entry = this.staged.get(stagedHandle);
    if (!entry) throw new Error("missing stage");
    this.values.set(entry.resource, this.corruptAfterApply ? { corrupt: true } : structuredClone(entry.value));
  }
  async rollback(backupHandle: string): Promise<void> {
    if (this.failRollback) throw new Error("rollback refused");
    const resource = backupHandle.slice("backup:".length);
    this.values.set(resource, structuredClone(this.backups.get(backupHandle)));
  }
}

async function planFor(transport: MemoryTransport) {
  transport.values.set("/etc/asterisk/pjsip.json", { port: 5060 });
  return await new StructuredConfigPlanner(now).createPlan(
    "plan-1",
    "target-1",
    [{ resource: "/etc/asterisk/pjsip.json", value: { port: 5061 } }],
    transport,
  );
}

test("configuration transaction backs up, stages, validates, applies, and post-reads", async () => {
  const transport = new MemoryTransport();
  const plan = await planFor(transport);
  const result = await new ConfigTransaction(transport, now).apply(plan);
  assert.equal(result.status, "applied");
  assert.deepEqual(transport.values.get("/etc/asterisk/pjsip.json"), { port: 5061 });
  assert.deepEqual(result.completedActions, [
    "backup:/etc/asterisk/pjsip.json",
    "stage:/etc/asterisk/pjsip.json",
    "validate:/etc/asterisk/pjsip.json",
    "apply:/etc/asterisk/pjsip.json",
    "post-read:/etc/asterisk/pjsip.json",
  ]);
});

test("validation failure leaves the live resource unchanged", async () => {
  const transport = new MemoryTransport();
  const plan = await planFor(transport);
  transport.failValidation = true;
  const result = await new ConfigTransaction(transport, now).apply(plan);
  assert.equal(result.status, "failed");
  assert.equal(result.rollbackAttempted, false);
  assert.deepEqual(transport.values.get("/etc/asterisk/pjsip.json"), { port: 5060 });
});

test("post-read mismatch triggers a successful rollback", async () => {
  const transport = new MemoryTransport();
  const plan = await planFor(transport);
  transport.corruptAfterApply = true;
  const result = await new ConfigTransaction(transport, now).apply(plan);
  assert.equal(result.status, "rolledBack");
  assert.equal(result.rollbackSucceeded, true);
  assert.match(result.message, /Post-read mismatch/u);
  assert.deepEqual(transport.values.get("/etc/asterisk/pjsip.json"), { port: 5060 });
});

test("rollback refusal is reported as a failed transaction", async () => {
  const transport = new MemoryTransport();
  const plan = await planFor(transport);
  transport.corruptAfterApply = true;
  transport.failRollback = true;
  const result = await new ConfigTransaction(transport, now).apply(plan);
  assert.equal(result.status, "failed");
  assert.equal(result.rollbackSucceeded, false);
});

test("pre-cancelled config transaction makes no transport mutation", async () => {
  const transport = new MemoryTransport();
  const plan = await planFor(transport);
  const controller = new AbortController();
  controller.abort();
  const result = await new ConfigTransaction(transport, now).apply(plan, controller.signal);
  assert.equal(result.status, "cancelled");
  assert.deepEqual(transport.values.get("/etc/asterisk/pjsip.json"), { port: 5060 });
});

test("capability snapshot preserves honest unavailable CLI, AMI, and ARI states", async () => {
  const unavailableAt = <T>(reason: string): CapabilityResult<T> => unavailable(reason, NOW.toISOString());
  const live: LiveAsteriskDiscovery = {
    async discoverIdentity() { return unavailableAt("Asterisk executable not found"); },
    async discoverCli() { return unavailableAt("CLI socket unavailable"); },
    async discoverAmi() { return unavailableAt("AMI is disabled"); },
    async discoverAri() { return unavailableAt("ARI is disabled"); },
  };
  const target: TargetProfile = { id: "target-1", displayName: "PBX", connectionKind: "wsl" };
  const snapshot = await buildCapabilitySnapshot(
    target,
    live,
    unavailableAt("OS probe unavailable"),
    unavailableAt("Storage probe unavailable"),
    unavailableAt("Privilege probe unavailable"),
    now,
  );
  assert.equal(snapshot.cli.state, "unavailable");
  assert.equal(snapshot.ami.reason, "AMI is disabled");
  assert.equal(snapshot.ari.reason, "ARI is disabled");
});

test("allowlisted live discovery parses Asterisk identity and CLI commands", async () => {
  const operations: string[] = [];
  const discovery = new AllowlistedAsteriskDiscovery({
    async execute(_target, operation) {
      operations.push(operation);
      if (operation === "identity") return command("Asterisk 22.4.1 built by builder\n");
      return command("core show version  Display version\npjsip show endpoints  List endpoints\n");
    },
  }, { now });
  const target: TargetProfile = { id: "target-1", displayName: "PBX", connectionKind: "localDocker" };
  assert.equal((await discovery.discoverIdentity(target)).value?.version, "22.4.1");
  assert.deepEqual(await discovery.discoverCli(target), {
    state: "available",
    observedAt: NOW.toISOString(),
    value: ["core show version", "pjsip show endpoints"],
  });
  assert.deepEqual(operations, ["identity", "cliCommands"]);
});

test("allowlisted live discovery reports missing AMI and ARI clients as unavailable", async () => {
  const discovery = new AllowlistedAsteriskDiscovery({ async execute() { return command(); } }, { now });
  const target: TargetProfile = { id: "target-1", displayName: "PBX", connectionKind: "remoteLinux" };
  assert.equal((await discovery.discoverAmi(target)).state, "unavailable");
  assert.equal((await discovery.discoverAri(target)).state, "unavailable");
});

test("AMI and ARI discovery normalizes unique names and redacts failure reasons", async () => {
  const target: TargetProfile = { id: "target-1", displayName: "PBX", connectionKind: "remoteDocker" };
  const discovery = new AllowlistedAsteriskDiscovery(
    { async execute() { return command(); } },
    {
      now,
      ami: { async discover() { return { version: "22", actions: ["Ping", "Ping", "CoreStatus"] }; } },
      ari: { async discover() { throw new Error("token=do-not-print refused"); } },
    },
  );
  assert.deepEqual((await discovery.discoverAmi(target)).value?.actions, ["CoreStatus", "Ping"]);
  const ari = await discovery.discoverAri(target);
  assert.equal(ari.state, "unavailable");
  assert.doesNotMatch(ari.reason ?? "", /do-not-print/u);
});
