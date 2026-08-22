export type RailId = 'pbx' | 'media' | 'data' | 'system' | 'automation' | 'app';
export type DestinationKind = 'dashboard' | 'table' | 'canvas' | 'builder' | 'settings' | 'history';
export interface Destination { id: string; rail: RailId; icon: string; label: string; title: string; source: string; kind: DestinationKind; description: string; }
export interface Rail { id: RailId; icon: string; label: string; description: string; }

export const rails: Rail[] = [
  { id:'pbx', icon:'☎', label:'PBX', description:'Endpoints, routing, and active calls' },
  { id:'media', icon:'◉', label:'Media', description:'Voice, prompts, and transport' },
  { id:'data', icon:'▥', label:'Data', description:'Records and interfaces' },
  { id:'system', icon:'⚙', label:'System', description:'Runtime, security, and diagnostics' },
  { id:'automation', icon:'◇', label:'Automation', description:'Local automation services' },
  { id:'app', icon:'✦', label:'App', description:'Deployment and console preferences' },
];

const d = (id:string, rail:RailId, icon:string, label:string, title:string, source:string, kind:DestinationKind, description:string): Destination => ({id,rail,icon,label,title,source,kind,description});
export const destinations: Destination[] = [
  d('dashboard','pbx','▦','Dashboard','Dashboard','live','dashboard','A read-only summary of the connected PBX.'),
  d('live-channels','pbx','≋','Live channels','Live channels','core show channels','table','Calls and media channels reported by the running service.'),
  d('endpoints','pbx','▯','Endpoints','PJSIP endpoints','pjsip.conf','table','Phones and applications registered with this PBX.'),
  d('trunks','pbx','⇄','Trunks','Trunks and registrations','pjsip.conf','table','Carrier and partner connections.'),
  d('trunk-authentication','pbx','♢','Trunk authentication','Trunk authentication','pjsip.conf','settings','Authentication policy for incoming partner requests.'),
  d('dialplan-canvas','pbx','⌘','Dialplan canvas','Dialplan canvas','extensions.conf','canvas','A visual call-routing graph with six nodes and five edges.'),
  d('ivr-menus','pbx','⌨','IVR menus','IVR menus','extensions.conf','table','Key-driven caller menus and prompt routing.'),
  d('queues-agents','pbx','♟','Queues & agents','Queues and agents','queues.conf','table','Waiting lines, ring strategies, and memberships.'),
  d('voicemail','media','◫','Voicemail','Voicemail boxes','voicemail.conf','table','Mailbox owners, delivery, and storage policy.'),
  d('conferences','media','◎','Conferences','Conference rooms','confbridge.conf','table','Bridge rooms, profiles, and recording policy.'),
  d('music-on-hold','media','♫','Music on hold','Music on hold','musiconhold.conf','table','Local media classes and playback order.'),
  d('codecs-rtp','media','≋','Codecs & RTP','Codecs and RTP','codecs.conf · rtp.conf','settings','Codec preference and RTP port policy.'),
  d('call-records','data','▤','CDR & CEL','Call records','cdr.conf · cel.conf','table','Call detail and event logging outputs.'),
  d('manager-rest','data','⌁','AMI & ARI','Manager and REST interfaces','manager.conf · ari.conf · http.conf','table','Machine interfaces and their permission matrices.'),
  d('modules','system','⬡','Modules','Modules','modules.conf','table','Loaded runtime modules and use counts.'),
  d('logger','system','▧','Logger','Logging','logger.conf','settings','Channels, rotation, and severity choices.'),
  d('security','system','⬟','Security','Security','acl.conf · stir_shaken.conf','settings','Access lists, TLS, identity, and hardening.'),
  d('cli-builder','system','⌁','CLI builder','CLI builder','asterisk -rx','builder','Guided read-only command construction.'),
  d('memory-console','automation','▣','Memory console','Memory console','local automation data','table','Local records exposed by the automation integration.'),
  d('sync-attestation','automation','↻','Sync & attestation','Sync and attestation','local automation sync','table','Local synchronization history and verification.'),
  d('skills-registry','automation','✣','Skills registry','Skills registry','local skills directory','table','Installed local capability packages.'),
  d('status-hub','automation','⌬','Status hub','Status sessions','status service','table','Active local work sessions and factual states.'),
  d('wording-guard','automation','▰','Wording guard','Wording and emission guard','local wording policy','settings','Private local wording policy state; content is never bundled.'),
  d('operations','automation','▲','Operations','Operations and releases','release records','table','Version, artifact, and duration records.'),
  d('secret-intake','automation','◆','Secret intake','Secret intake','local secure intake','table','One-time local intake records; values are never shown.'),
  d('deploy-servers','app','▣','Deploy & servers','Deploy a server','provisioning','builder','Guided server connection and provisioning.'),
  d('confirmation-credits','app','◈','Confirmation credits','Confirmation credits','local confirmations','dashboard','Optional local activities that reduce repetitive confirmation steps.'),
  d('notifications','app','●','Notifications','Notification centre','console','table','Reviewable local notification history.'),
  d('history','app','↶','History','Configuration history','local version history','history','Append-only local configuration revisions.'),
  d('customise','app','✦','Customise','Customise everything','console profile','settings','Element-level appearance, layout, and behavior.'),
  d('appearance','app','◐','Appearance','Appearance','console settings','settings','Theme, density, accent, type, and motion.'),
  d('about','app','ⓘ','About','About Ding PBX Console','application metadata','settings','Version, integration boundary, and policy disclosures.'),
];

export const rowsByDestination: Record<string, string[][]> = {
  'live-channels': [['PJSIP/1001','1001','Dial','00:03:12','Up'],['PJSIP/1004','Support queue','Queue','00:00:28','Ringing']],
  endpoints: [['1001','10.20.4.21','TLS','opus, ulaw','Available'],['1003','10.20.4.44','UDP','ulaw','Unavailable']],
  trunks: [['Primary carrier','sip.example.invalid','Digest','+1555…','Registered'],['Partner lab','10.0.4.2','IP ACL','Internal','Available']],
  'queues-agents': [['Support','roundrobin','6','2','91%'],['Sales','fewestcalls','4','0','98%']],
  voicemail: [['1001','A. Example','local delivery','2','18 MB'],['1002','B. Example','local delivery','0','4 MB']],
};
