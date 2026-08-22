import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";

const ROOT = new URL("../../control-plane/", import.meta.url);

test("negative contract: no shell execution or arbitrary command API is present", async () => {
  const executor = await readFile(new URL("executor.ts", ROOT), "utf8");
  assert.match(executor, /shell:\s*false/u);
  assert.doesNotMatch(executor, /shell:\s*true/u);
  assert.doesNotMatch(executor, /exec\s*\(/u);
});

test("negative contract: SSH policy forbids disabled verification and ephemeral host stores", async () => {
  const ssh = await readFile(new URL("ssh.ts", ROOT), "utf8");
  assert.match(ssh, /StrictHostKeyChecking=accept-new/u);
  assert.match(ssh, /UpdateHostKeys=no/u);
  assert.doesNotMatch(ssh, /StrictHostKeyChecking=(?:no|off)/u);
  assert.doesNotMatch(ssh, /UserKnownHostsFile=\/dev\/null/u);
});

test("negative contract: every required public contract is exported", async () => {
  const contracts = await readFile(new URL("contracts.ts", ROOT), "utf8");
  assertContractsExported(contracts);
});

test("negative contract self-test turns red when one required export is removed", async () => {
  const contracts = await readFile(new URL("contracts.ts", ROOT), "utf8");
  const broken = contracts.replace(/^export interface ProvisionJob/mu, "interface ProvisionJob");
  assert.notEqual(broken, contracts, "deliberate break must modify the fixture");
  assert.throws(() => assertContractsExported(broken), /ProvisionJob/u);
  assert.doesNotThrow(() => assertContractsExported(contracts));
});

function assertContractsExported(contracts: string): void {
  for (const name of [
    "TargetProfile",
    "ConnectionKind",
    "AsteriskIdentity",
    "CapabilitySnapshot",
    "ChangePlan",
    "ApplyResult",
    "ProvisionJob",
  ]) {
    const pattern = new RegExp(`^export (?:interface|type) ${name}(?:\\s|<|=)`, "m");
    assert.ok(pattern.test(contracts), `Missing required export: ${name}`);
  }
}
