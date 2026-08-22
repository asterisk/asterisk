export type ControlPlaneAction =
  | 'server.list' | 'server.connect' | 'pbx.snapshot' | 'pbx.apply'
  | 'pbx.command' | 'history.list' | 'history.restore';

export interface ControlPlaneRequest {
  requestId: string;
  action: ControlPlaneAction;
  serverId?: string;
  payload?: Readonly<Record<string, unknown>>;
}

export type ControlPlaneResponse =
  | { ok: true; requestId: string; data: unknown }
  | { ok: false; requestId: string; code: string; message: string };

export interface DingDesktopApi {
  platform: string;
  window: { minimize(): void; toggleMaximize(): void; close(): void };
  controlPlane: { request(request: ControlPlaneRequest): Promise<ControlPlaneResponse> };
}

declare global { interface Window { dingDesktop?: DingDesktopApi } }
