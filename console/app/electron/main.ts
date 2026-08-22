import { app, BrowserWindow, ipcMain } from 'electron';
import { join } from 'node:path';
import { NodeProcessExecutor, TargetDiscovery } from '../../control-plane/index.js';
import type { ControlPlaneRequest, ControlPlaneResponse } from '../../shared/control-plane.js';

let mainWindow: BrowserWindow | null = null;
const processExecutor = new NodeProcessExecutor({ allowedExecutables: ['wsl.exe', 'docker'] });
const targetDiscovery = new TargetDiscovery(processExecutor);

async function controlPlaneRequest(request: ControlPlaneRequest): Promise<ControlPlaneResponse> {
  try {
    if (request.action === 'server.list') {
      const [wsl, containers] = await Promise.all([
        targetDiscovery.discoverWslDistributions().catch(error => ({ unavailable: error instanceof Error ? error.message : 'WSL discovery failed' })),
        targetDiscovery.discoverLocalDocker('ding-pbx-console').catch(error => ({ unavailable: error instanceof Error ? error.message : 'Docker discovery failed' })),
      ]);
      return { ok: true, requestId: request.requestId, data: { observedAt: new Date().toISOString(), wsl, containers } };
    }
    if (request.action === 'server.connect' || request.action === 'pbx.snapshot') {
      const distribution = request.serverId?.trim();
      if (!distribution) return { ok: false, requestId: request.requestId, code: 'TARGET_REQUIRED', message: 'Select a discovered WSL distribution.' };
      const discovered = await targetDiscovery.discoverWslDistributions();
      if (!discovered.includes(distribution)) return { ok: false, requestId: request.requestId, code: 'TARGET_NOT_DISCOVERED', message: 'The WSL distribution is not in the current discovery result.' };
      const [os, asterisk] = await Promise.all([
        processExecutor.execute({ executable: 'wsl.exe', args: ['-d', distribution, '--', 'cat', '/etc/os-release'], timeoutMs: 10_000 }),
        processExecutor.execute({ executable: 'wsl.exe', args: ['-d', distribution, '--', 'asterisk', '-rx', 'core show version'], timeoutMs: 10_000 }),
      ]);
      return { ok: true, requestId: request.requestId, data: {
        target: { connectionKind: 'wsl', distribution },
        operatingSystem: os.status === 'succeeded' ? targetDiscovery.parseDebianOperatingSystem(os.stdout) : { state: 'unavailable', reason: os.stderr, observedAt: new Date().toISOString() },
        asterisk: asterisk.status === 'succeeded' ? { state: 'available', value: asterisk.stdout.trim(), observedAt: new Date().toISOString() } : { state: 'unavailable', reason: asterisk.stderr || 'Asterisk is not installed or not running.', observedAt: new Date().toISOString() },
      } };
    }
    return { ok: false, requestId: request.requestId, code: 'ACTION_NOT_AVAILABLE', message: 'This operation is unavailable until a reviewed target-specific plan is connected.' };
  } catch (error) {
    return { ok: false, requestId: request.requestId, code: 'CONTROL_PLANE_ERROR', message: error instanceof Error ? error.message : 'Control-plane request failed.' };
  }
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 920,
    minWidth: 920,
    minHeight: 640,
    frame: false,
    backgroundColor: '#101510',
    show: false,
    title: 'Ding PBX Console',
    webPreferences: {
      preload: join(import.meta.dirname, '../../../app/electron/preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  mainWindow.once('ready-to-show', () => mainWindow?.show());
  if (process.env.VITE_DEV_SERVER_URL) mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL);
  else mainWindow.loadFile(join(import.meta.dirname, '../../../dist/index.html'));
}

ipcMain.on('window:minimize', () => mainWindow?.minimize());
ipcMain.on('window:toggle-maximize', () => mainWindow?.isMaximized() ? mainWindow.unmaximize() : mainWindow?.maximize());
ipcMain.on('window:close', () => mainWindow?.close());
ipcMain.handle('control-plane:request', async (_event, request: ControlPlaneRequest) => controlPlaneRequest(request));

app.whenReady().then(createWindow);
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
