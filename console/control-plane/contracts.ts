export const CONNECTION_KINDS = [
  "wsl",
  "localDocker",
  "remoteLinux",
  "remoteDocker",
] as const;

export type ConnectionKind = (typeof CONNECTION_KINDS)[number];

export interface TargetProfile {
  id: string;
  displayName: string;
  connectionKind: ConnectionKind;
  host?: string;
  port?: number;
  user?: string;
  wslDistribution?: string;
  dockerContext?: string;
  dockerProject?: string;
  knownHostsPath?: string;
}

export interface AsteriskIdentity {
  version: string;
  systemName?: string;
  entityId?: string;
  processId?: number;
  executable?: string;
  configDirectory?: string;
}

export type CapabilityState = "available" | "unavailable" | "unknown";

export interface CapabilityResult<T = unknown> {
  state: CapabilityState;
  value?: T;
  reason?: string;
  observedAt: string;
}

export interface CapabilitySnapshot {
  targetId: string;
  observedAt: string;
  operatingSystem: CapabilityResult<OperatingSystemIdentity>;
  asterisk: CapabilityResult<AsteriskIdentity>;
  cli: CapabilityResult<ReadonlyArray<string>>;
  ami: CapabilityResult<{ version?: string; actions: ReadonlyArray<string> }>;
  ari: CapabilityResult<{ version?: string; resources: ReadonlyArray<string> }>;
  freeStorageBytes: CapabilityResult<number>;
  canElevate: CapabilityResult<boolean>;
}

export interface OperatingSystemIdentity {
  id: "debian" | "ubuntu";
  versionId: string;
  prettyName: string;
}

export type PlanActionKind =
  | "backup"
  | "stage"
  | "validate"
  | "apply"
  | "postRead"
  | "rollback"
  | "installPackage"
  | "fetchSource"
  | "checkoutCommit"
  | "build"
  | "restartService";

export interface PlanAction {
  id: string;
  kind: PlanActionKind;
  description: string;
  resource?: string;
  details?: Readonly<Record<string, string | number | boolean>>;
}

export interface StructuredDiff {
  resource: string;
  before: unknown;
  after: unknown;
  changedPaths: ReadonlyArray<string>;
}

export interface ChangePlan {
  id: string;
  targetId: string;
  createdAt: string;
  summary: string;
  actions: ReadonlyArray<PlanAction>;
  diffs: ReadonlyArray<StructuredDiff>;
  requiredStorageBytes: number;
  destructive: boolean;
}

export type ApplyStatus =
  | "applied"
  | "rolledBack"
  | "failed"
  | "cancelled"
  | "unavailable";

export interface ApplyResult {
  planId: string;
  status: ApplyStatus;
  startedAt: string;
  finishedAt: string;
  completedActions: ReadonlyArray<string>;
  failedAction?: string;
  rollbackAttempted: boolean;
  rollbackSucceeded?: boolean;
  message: string;
}

export type ProvisionJobState =
  | "queued"
  | "running"
  | "succeeded"
  | "failed"
  | "cancelled";

export interface ProvisionJob {
  id: string;
  targetId: string;
  sourceRepository: string;
  sourceCommit: string;
  state: ProvisionJobState;
  createdAt: string;
  startedAt?: string;
  finishedAt?: string;
  progressPercent: number;
  currentAction?: string;
  message?: string;
}
