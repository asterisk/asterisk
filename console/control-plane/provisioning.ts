import type { ChangePlan, OperatingSystemIdentity, PlanAction } from "./contracts.js";

export interface ProvisioningRequest {
  id: string;
  targetId: string;
  operatingSystem: OperatingSystemIdentity;
  repository: string;
  commit: string;
  sourceDirectory: string;
  installPrefix: string;
  requiredStorageBytes: number;
}

const PACKAGES = [
  "build-essential",
  "git",
  "libedit-dev",
  "libjansson-dev",
  "libsqlite3-dev",
  "libssl-dev",
  "libxml2-dev",
  "uuid-dev",
] as const;

export class SourceProvisioningPlanner {
  readonly now: () => Date;

  constructor(now = () => new Date()) {
    this.now = now;
  }

  createPlan(request: ProvisioningRequest, freeStorageBytes: number, canElevate: boolean): ChangePlan {
    validateRequest(request);
    if (!canElevate) throw new Error("Non-interactive privilege elevation is unavailable");
    if (!Number.isSafeInteger(freeStorageBytes) || freeStorageBytes < request.requiredStorageBytes) {
      throw new Error(
        `Insufficient storage: ${freeStorageBytes} available, ${request.requiredStorageBytes} required`,
      );
    }
    const actions: ReadonlyArray<PlanAction> = [
      {
        id: "install-packages",
        kind: "installPackage",
        description: "Install allowlisted Asterisk build packages",
        details: { packages: PACKAGES.join(","), operatingSystem: request.operatingSystem.id },
      },
      {
        id: "fetch-source",
        kind: "fetchSource",
        description: "Fetch Asterisk source from the configured HTTPS repository",
        resource: request.repository,
        details: { sourceDirectory: request.sourceDirectory },
      },
      {
        id: "checkout-commit",
        kind: "checkoutCommit",
        description: "Detach the source tree at the exact requested commit",
        details: { commit: request.commit },
      },
      {
        id: "build-source",
        kind: "build",
        description: "Configure and build the pinned Asterisk source",
        details: { installPrefix: request.installPrefix },
      },
    ];
    return {
      id: request.id,
      targetId: request.targetId,
      createdAt: this.now().toISOString(),
      summary: `Provision Asterisk from exact commit ${request.commit}`,
      actions,
      diffs: [],
      requiredStorageBytes: request.requiredStorageBytes,
      destructive: false,
    };
  }
}

export function buildProvisioningRecipe(request: ProvisioningRequest): ReadonlyArray<ReadonlyArray<string>> {
  validateRequest(request);
  return [
    ["apt-get", "update"],
    ["apt-get", "install", "--yes", "--no-install-recommends", ...PACKAGES],
    ["git", "clone", "--filter=blob:none", "--no-checkout", request.repository, request.sourceDirectory],
    ["git", "-C", request.sourceDirectory, "fetch", "--depth=1", "origin", request.commit],
    ["git", "-C", request.sourceDirectory, "checkout", "--detach", request.commit],
    ["./configure", `--prefix=${request.installPrefix}`],
    ["make", "-j2"],
    ["make", "install"],
  ];
}

function validateRequest(request: ProvisioningRequest): void {
  if (request.operatingSystem.id !== "debian" && request.operatingSystem.id !== "ubuntu") {
    throw new Error("Only Debian and Ubuntu provisioning recipes are supported");
  }
  if (!/^https:\/\/[a-zA-Z0-9.-]+\/[a-zA-Z0-9._/-]+(?:\.git)?$/u.test(request.repository)) {
    throw new Error("Source repository must be an HTTPS Git URL");
  }
  if (!/^[0-9a-f]{40}$/u.test(request.commit)) throw new Error("Source commit must be a full SHA-1");
  if (!/^\/[a-zA-Z0-9._/-]+$/u.test(request.sourceDirectory)) throw new Error("Invalid source directory");
  if (!/^\/[a-zA-Z0-9._/-]+$/u.test(request.installPrefix)) throw new Error("Invalid install prefix");
  if (!Number.isSafeInteger(request.requiredStorageBytes) || request.requiredStorageBytes <= 0) {
    throw new Error("requiredStorageBytes must be a positive integer");
  }
}
