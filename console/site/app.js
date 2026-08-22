(() => {
  'use strict';

  const DESTINATIONS = [
    {id:'dash',name:'Dashboard',icon:'⌂',group:'PBX',article:'overview/dashboard',description:'System summary, alerts, recent activity, and guided next actions.'},
    {id:'live',name:'Live channels',icon:'◉',group:'PBX',article:'overview/live-calls',description:'Documented real-time call state, channels, bridges, and privacy boundaries.'},
    {id:'endpoints',name:'PJSIP endpoints',icon:'▣',group:'PBX',article:'people-devices/devices',description:'Phones and applications registered with the PBX.'},
    {id:'trunks',name:'Trunks & registrations',icon:'⇄',group:'PBX',article:'connectivity/trunks',description:'Provider connections, transports, registrations, and failover.'},
    {id:'trunkauth',name:'Trunk authentication',icon:'◇',group:'PBX',article:'team-calling/security',description:'Authentication policy for incoming partner requests.'},
    {id:'canvas',name:'Dialplan canvas',icon:'⌁',group:'PBX',article:'call-flow/call-flow',description:'Visual call-path composition with validation and reversible publishing.'},
    {id:'ivr',name:'IVR menus',icon:'⌘',group:'PBX',article:'call-flow/ivr',description:'Menus, prompts, timeouts, invalid choices, accessibility, and testing.'},
    {id:'queues',name:'Queues & agents',icon:'☷',group:'PBX',article:'team-calling/queues',description:'Agents, distribution strategies, wait states, announcements, and reporting.'},

    {id:'voicemail',name:'Voicemail boxes',icon:'▻',group:'Media',article:'people-devices/voicemail',description:'Mailboxes, greetings, delivery, retention, access, and recovery.'},
    {id:'confbridge',name:'ConfBridge rooms',icon:'◌',group:'Media',article:'team-calling/conferences',description:'Rooms, moderators, access, prompts, recording, and capacity.'},
    {id:'moh',name:'Music on hold',icon:'♬',group:'Media',article:'team-calling/announcements',description:'Local media classes, ordering, volume, and fallback behavior.'},
    {id:'codecs',name:'Codecs & RTP',icon:'≋',group:'Media',article:'connectivity/extensions',description:'Codec preferences, media transport, port policy, and compatibility.'},

    {id:'cdr',name:'Call records',icon:'≡',group:'Data',article:'overview/cdr',description:'Searchable call records, filters, privacy controls, and redacted export guidance.'},
    {id:'ami',name:'Manager & REST interfaces',icon:'⌁',group:'Data',article:'manage/automation',description:'AMI, ARI, and HTTP capabilities with bounded access guidance.'},

    {id:'modules',name:'Modules',icon:'⬡',group:'System',article:'overview/system-health',description:'Loaded runtime modules, dependencies, and use counts.'},
    {id:'logger',name:'Logging',icon:'▤',group:'System',article:'overview/logs',description:'Search, filtering, severity, retention, and diagnostic export guidance.'},
    {id:'security',name:'Security',icon:'◇',group:'System',article:'team-calling/security',description:'Transport protection, credentials, access rules, auditing, and recovery.'},
    {id:'cli',name:'CLI builder',icon:'⌨',group:'System',article:'overview/status',description:'Guided allowlisted diagnostic command construction.'},

    {id:'memory',name:'Memory console',icon:'▣',group:'Agent',article:'manage/backups',description:'Local records, append-only history, and recovery boundaries.'},
    {id:'sync',name:'Sync & attestation',icon:'↻',group:'Agent',article:'manage/updates',description:'Local synchronization history and factual verification state.'},
    {id:'skills',name:'Skills registry',icon:'✣',group:'Agent',article:'manage/automation',description:'Installed local capability packages and their evidence.'},
    {id:'hub',name:'Status hub sessions',icon:'◆',group:'Agent',article:'overview/status',description:'Active work sessions and factual current states.'},
    {id:'vocab',name:'Vocabulary settings',icon:'▰',group:'Agent',article:'manage/settings',description:'Private local wording configuration without bundled personal mappings.'},
    {id:'ops',name:'Operations & releases',icon:'▲',group:'Agent',article:'overview/reports',description:'Version, artifact, duration, and release evidence.'},
    {id:'secrets',name:'Secret intake',icon:'◆',group:'Agent',article:'team-calling/security',description:'One-time local intake guidance; values are never displayed.'},

    {id:'servers',name:'Deploy a server',icon:'▣',group:'App',article:'manage/automation',description:'Guided WSL, local container, and approved remote Linux provisioning.'},
    {id:'arcade',name:'Confirmation credits',icon:'◈',group:'App',article:'manage/accessibility',description:'Optional local activities that reduce repetitive confirmation steps.'},
    {id:'notifications',name:'Notification centre',icon:'●',group:'App',article:'overview/status',description:'Reviewable local notifications, filtering, dismissal, and export.'},
    {id:'history',name:'History',icon:'↶',group:'App',article:'manage/backups',description:'Append-only local configuration revisions, comparison, and restore.'},
    {id:'customise',name:'Customise everything',icon:'✦',group:'App',article:'manage/appearance',description:'Element-level appearance, layout, behavior, and local reset.'},
    {id:'appearance',name:'Appearance',icon:'◐',group:'App',article:'manage/appearance',description:'Theme, density, typography, accent, logo, and element editors.'},
    {id:'about',name:'About',icon:'ⓘ',group:'App',article:'overview/documentation',description:'Version, integration boundaries, project status, and documentation.'}
  ];

  const CONVERTERS = [
    ['Documents/PDF','PDF inspect, split, merge, extract, reorder, rotate, metadata','Unavailable','Desktop adapter and packaged proof are not published.'],
    ['Images','PNG, JPEG, WebP, SVG','Unavailable','No isolated packaged decoder is available on this static page.'],
    ['Audio','WAV, FLAC, Opus','Unavailable','No bundled offline audio adapter is published.'],
    ['Video','MP4, WebM','Unavailable','No bundled offline video adapter is published.'],
    ['Archives','ZIP, 7z','Unavailable','No packaged archive adapter or validation proof exists.'],
    ['Structured Data/Spreadsheets','JSON, JSONL, YAML, TOML, XML, CSV, TSV','Preview only','The desktop conversion path is documented but not available here.'],
    ['Code/Text','Markdown, HTML, JavaScript, TypeScript, Python, Go, Rust','Preview only','This catalogue describes planned adapters; it does not write files.'],
    ['Binary Encodings','Base64, hexadecimal','Unavailable','No conversion runs on this documentation surface.']
  ];

  const DEFAULTS = {
    dock:'left', activeTab:'dash', pinned:['dash'], closed:[], language:'en', englishFunny:5,
    cantoneseFunny:5, dialogEmoji:true, theme:'dark', accent:'#9f86ff', density:'comfortable',
    fontScale:100, displayName:'Ding PBX Console', attention:{focus:false,low:false,time:false,one:false,momentum:false},
    nextAction:'', notifications:[], tickets:[], schedule:{enabled:false,days:[]}, locks:[]
  };
  const STORAGE_KEY = 'ding-pbx-site-v1';
  const state = loadState();
  const regexState = new Map();
  let regexTarget = null;
  let contextTab = null;

  function $(id){ return document.getElementById(id); }
  function loadState(){
    try { return merge(DEFAULTS, JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}')); }
    catch { return structuredClone(DEFAULTS); }
  }
  function merge(base, saved){ return {...structuredClone(base), ...saved, attention:{...base.attention,...(saved.attention||{})}, schedule:{...base.schedule,...(saved.schedule||{})}}; }
  function saveState(){ localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); }
  function escapeHtml(value){ return String(value).replace(/[&<>'"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[c])); }
  function articlePath(item){ return `../docs/${item.article}.md`; }

  function init(){
    applyState(); renderTabs(); renderDestinations(); renderConverters(); renderAttention(); renderTickets(); renderNotifications(); renderPalette(''); enhanceDropdowns(); bindEvents(); updateTimeAwareness(); setInterval(updateTimeAwareness, 60000);
  }
  function applyState(){
    document.documentElement.dataset.theme=state.theme; document.documentElement.dataset.density=state.density; document.documentElement.dataset.dock=state.dock;
    document.documentElement.style.setProperty('--primary',state.accent); document.documentElement.style.setProperty('--font-scale',String(state.fontScale/100));
    $('dock-select').value=state.dock; $('language-mode').value=state.language; $('english-funny').value=state.englishFunny; $('cantonese-funny').value=state.cantoneseFunny;
    $('english-funny-output').textContent=state.englishFunny; $('cantonese-funny-output').textContent=state.cantoneseFunny; $('dialog-emoji').checked=state.dialogEmoji;
    $('theme-mode').value=state.theme; $('accent-color').value=state.accent; $('density-mode').value=state.density; $('font-scale').value=state.fontScale; $('font-scale-output').textContent=`${state.fontScale}%`;
    $('display-name').textContent=state.displayName; $('display-name-input').value=state.displayName; $('next-action').value=state.nextAction; applyLanguage();
    document.body.classList.toggle('focus-mode',state.attention.focus); document.body.classList.toggle('low-stimulation',state.attention.low); document.body.classList.toggle('one-at-a-time',state.attention.one); document.body.classList.toggle('momentum-mode',state.attention.momentum);
  }
  function applyLanguage(){
    const englishTitle='Know what your phone system is doing.', cantoneseTitle='睇清楚你個電話系統做緊乜。';
    const englishCopy='Ding PBX Console is a planned desktop administration experience for Asterisk. This website is a landing, documentation, download, status, settings, and link surface only. It is not the installed desktop application and it is not a PBX runtime.';
    const cantoneseCopy='Ding PBX Console 係為 Asterisk 規劃嘅桌面管理體驗。呢個網站只提供介紹、文件、下載狀態、設定同連結；佢唔係已安裝嘅桌面程式，亦唔係 PBX 執行環境。';
    document.documentElement.lang=state.language==='zh'?'zh-Hant':'en';
    $('hero-title').textContent=state.language==='en'?englishTitle:state.language==='zh'?cantoneseTitle:`${englishTitle} / ${cantoneseTitle}`;
    $('hero-copy').textContent=state.language==='en'?englishCopy:state.language==='zh'?cantoneseCopy:`${englishCopy} / ${cantoneseCopy}`;
    $('language-preview').textContent=state.language==='en'?'English presentation active.':state.language==='zh'?'廣東話顯示已啟用。':'Bilingual presentation active. / 雙語顯示已啟用。';
  }

  function renderTabs(){
    const list=$('tab-list'); list.innerHTML='';
    let lastGroup='';
    [...DESTINATIONS].sort((a,b)=>Number(state.pinned.includes(b.id))-Number(state.pinned.includes(a.id))).filter(x=>!state.closed.includes(x.id)).forEach(item=>{
      if(item.group!==lastGroup){ const heading=document.createElement('div'); heading.className='tab-group'; heading.textContent=item.group; list.append(heading); lastGroup=item.group; }
      const tab=document.createElement('button'); tab.className='tab'; tab.id=`tab-${item.id}`; tab.role='tab'; tab.dataset.id=item.id; tab.setAttribute('aria-selected',String(state.activeTab===item.id)); tab.setAttribute('aria-controls',`destination-${item.id}`);
      tab.innerHTML=`<span class="tab-icon" aria-hidden="true">${item.icon}</span><span>${escapeHtml(item.name)}</span>${state.pinned.includes(item.id)?'<span class="pin" aria-label="Pinned">●</span>':''}`;
      tab.addEventListener('click',()=>activateDestination(item.id)); tab.addEventListener('contextmenu',e=>openContext(e,item.id)); tab.addEventListener('keydown',onTabKeydown); list.append(tab);
    });
    renderManagedTabs();
  }
  function renderDestinations(){
    $('destination-grid').innerHTML=DESTINATIONS.map(item=>`<article class="destination-card" id="destination-${item.id}" tabindex="-1" data-search="${escapeHtml(`${item.name} ${item.group} ${item.description}`)}"><span aria-hidden="true">${item.icon}</span><h3>${escapeHtml(item.name)}</h3><p>${escapeHtml(item.description)}</p><a class="text-button" href="${articlePath(item)}">Read article</a></article>`).join('');
  }
  function renderConverters(){ $('converter-catalog').innerHTML=CONVERTERS.map(([name,formats,status,reason])=>`<article class="catalog-card" data-search="${escapeHtml([name,formats,status,reason].join(' '))}"><span class="status-chip">${escapeHtml(status)}</span><h3>${escapeHtml(name)}</h3><p>${escapeHtml(formats)}</p><small>${escapeHtml(reason)}</small></article>`).join(''); }
  function renderAttention(){
    const modes=[['focus','Focus','Bring the current surface forward without hiding the rest.'],['low','Low stimulation','Reduce non-essential motion and visual intensity.'],['time','Time awareness','Show exact elapsed time without nagging.'],['one','One thing at a time','De-emphasize destinations beyond the chosen next action.'],['momentum','Momentum','Allow gentle, dismissible inactivity prompts.']];
    $('attention-settings').innerHTML=modes.map(([id,label,desc])=>`<div class="setting-row" data-search="${label} ${desc}"><div><h3>${label}</h3><p>${desc}</p></div><input type="checkbox" data-attention="${id}" ${state.attention[id]?'checked':''} aria-label="${label}"></div>`).join('');
  }
  function renderTickets(){ $('ticket-list').innerHTML=state.tickets.length?state.tickets.map(t=>`<div class="ticket"><strong>${escapeHtml(t.id)} · ${escapeHtml(t.category)}</strong><p>${escapeHtml(t.description||'No description supplied.')}</p><small>${escapeHtml(t.status)} · local browser record</small></div>`).join(''):'<p>No local tickets.</p>'; }
  function renderNotifications(query=''){
    const items=state.notifications.filter(n=>matchText(`${n.title} ${n.body}`,query,'notification-search'));
    $('notification-history').innerHTML=items.length?items.map(n=>`<div class="ticket"><strong>${escapeHtml(n.title)}</strong><p>${escapeHtml(n.body)}</p><small>${new Date(n.time).toLocaleString()}</small></div>`).join(''):'<p>No matching notifications.</p>';
    $('notification-count').textContent=state.notifications.length;
  }
  function renderManagedTabs(query=''){
    if(!$('managed-tab-list')) return;
    const source=DESTINATIONS.filter(x=>matchText(`${x.name} ${x.group}`,query,'master-tab-search'));
    $('managed-tab-list').innerHTML=source.map(x=>`<div class="setting-row"><div><strong>${escapeHtml(x.name)}</strong><p>${escapeHtml(x.group)} · ${state.closed.includes(x.id)?'Closed':'Open'} · ${state.pinned.includes(x.id)?'Pinned':'Unpinned'}</p></div><button type="button" class="text-button managed-pin" data-id="${x.id}">${state.pinned.includes(x.id)?'Unpin':'Pin'}</button><button type="button" class="text-button managed-open" data-id="${x.id}">${state.closed.includes(x.id)?'Reopen':'Open'}</button></div>`).join('');
  }
  function renderPalette(query){
    const settingCommands=[['Open site settings','settings'],['Open appearance studio','appearance'],['Open local AI documentation','local-ai'],['Open security demonstrations','security']];
    const commands=[...DESTINATIONS.map(x=>[x.name,x.id]),...settingCommands].filter(([name])=>matchText(name,query,'palette-search'));
    $('palette-results').innerHTML=commands.length?commands.map(([name,id])=>`<button type="button" class="palette-result" role="option" data-target="${id}"><strong>${escapeHtml(name)}</strong><span>Open and focus destination</span></button>`).join(''):'<p>No matching commands.</p>';
  }
  function enhanceDropdowns(){
    document.querySelectorAll('select').forEach((select,index)=>{
      if(select.closest('.searchable-select'))return;
      if(!select.id)select.id=`select-${index}`;
      const wrapper=document.createElement('span');wrapper.className='searchable-select';
      const filter=document.createElement('input');filter.type='search';filter.id=`${select.id}-filter`;filter.placeholder='Filter choices';filter.setAttribute('aria-label',`Filter choices for ${select.getAttribute('aria-label')||select.id}`);
      const trigger=document.createElement('button');trigger.type='button';trigger.className='regex-trigger';trigger.dataset.regexFor=filter.id;trigger.textContent='.*';trigger.setAttribute('aria-label',`Build a regular expression for ${filter.getAttribute('aria-label')}`);
      select.parentNode.insertBefore(wrapper,select);wrapper.append(filter,trigger,select);
      filter.addEventListener('input',()=>{[...select.options].forEach(option=>option.hidden=!matchText(option.textContent,filter.value,filter.id));const visible=[...select.options].filter(option=>!option.hidden);if(visible.length&&!visible.includes(select.selectedOptions[0]))select.value=visible[0].value});
    });
  }
  function activateDestination(id){
    state.activeTab=id; state.closed=state.closed.filter(x=>x!==id); saveState(); renderTabs();
    const target=$(`destination-${id}`) || (id==='dash'?$('destination-home'):null); if(target){ target.scrollIntoView({behavior:reduceMotion()?'auto':'smooth',block:'start'}); target.focus({preventScroll:true}); target.classList.add('highlight'); setTimeout(()=>target.classList.remove('highlight'),1500); }
    $('rail').classList.remove('open');
  }
  function onTabKeydown(event){
    const tabs=[...document.querySelectorAll('.tab')]; const current=tabs.indexOf(event.currentTarget); const vertical=['left','right'].includes(state.dock); const prev=vertical?'ArrowUp':'ArrowLeft'; const next=vertical?'ArrowDown':'ArrowRight';
    if(event.key===prev||event.key===next){ event.preventDefault(); tabs[(current+(event.key===next?1:-1)+tabs.length)%tabs.length].focus(); }
    if(event.key==='Home'){event.preventDefault();tabs[0]?.focus()} if(event.key==='End'){event.preventDefault();tabs.at(-1)?.focus()}
    if(event.shiftKey&&event.key==='F10'){event.preventDefault();const rect=event.currentTarget.getBoundingClientRect();openContext({preventDefault(){},clientX:rect.left+20,clientY:rect.bottom},event.currentTarget.dataset.id)}
  }

  function bindEvents(){
    $('rail-toggle').onclick=()=> $('rail').classList.toggle('open'); $('palette-open').onclick=openPalette; $('notification-open').onclick=()=>{$('notifications-dialog').showModal();renderNotifications()}; $('tab-manager-open').onclick=()=>{$('tab-manager').showModal();renderManagedTabs()};
    document.addEventListener('keydown',e=>{if(e.ctrlKey&&e.shiftKey&&e.key.toLowerCase()==='f'){e.preventDefault();openPalette()} if(e.key==='Escape'){$('context-menu').hidden=true;$('rail').classList.remove('open')}});
    $('tab-search').oninput=e=>filterElements('.tab',e.target.value,'tab-search'); $('feature-search').oninput=e=>filterElements('.destination-card',e.target.value,'feature-search'); $('converter-search').oninput=e=>filterElements('.catalog-card',e.target.value,'converter-search'); $('settings-search').oninput=e=>filterElements('.setting-row',e.target.value,'settings-search');
    $('palette-search').oninput=e=>renderPalette(e.target.value); $('notification-search').oninput=e=>renderNotifications(e.target.value); $('master-tab-search').oninput=e=>renderManagedTabs(e.target.value);
    $('dock-select').onchange=e=>update('dock',e.target.value,()=>{applyState();renderTabs()}); $('language-mode').onchange=e=>update('language',e.target.value,()=>{applyLanguage();notify('Language saved','This static preview persists the selected language mode.')});
    $('english-funny').oninput=e=>{state.englishFunny=Number(e.target.value);$('english-funny-output').textContent=e.target.value;saveState()}; $('cantonese-funny').oninput=e=>{state.cantoneseFunny=Number(e.target.value);$('cantonese-funny-output').textContent=e.target.value;saveState()}; $('dialog-emoji').onchange=e=>update('dialogEmoji',e.target.checked);
    $('theme-mode').onchange=e=>update('theme',e.target.value,applyState); $('accent-color').oninput=e=>update('accent',e.target.value,applyState); $('density-mode').onchange=e=>update('density',e.target.value,applyState); $('font-scale').oninput=e=>{state.fontScale=Number(e.target.value);saveState();applyState()};
    $('display-name-input').onchange=e=>update('displayName',e.target.value.trim()||DEFAULTS.displayName,applyState); $('display-name-reset').onclick=()=>update('displayName',DEFAULTS.displayName,applyState); $('next-action').onchange=e=>update('nextAction',e.target.value);
    document.querySelectorAll('[data-settings-tab]').forEach(btn=>btn.onclick=()=>selectSettingsTab(btn.dataset.settingsTab)); $('attention-settings').onchange=e=>{if(e.target.dataset.attention){state.attention[e.target.dataset.attention]=e.target.checked;saveState();applyState()}};
    document.querySelectorAll('.regex-trigger').forEach(btn=>btn.onclick=e=>{e.preventDefault();openRegex(btn.dataset.regexFor)}); $('regex-apply').onclick=e=>{e.preventDefault();applyRegex()}; document.querySelectorAll('[data-insert]').forEach(btn=>btn.onclick=()=>insertRegex(btn.dataset.insert)); $('regex-pattern').oninput=previewRegex;
    $('vocabulary-file').onchange=loadVocabulary; $('vocabulary-clear').onclick=clearVocabulary; $('export-settings').onclick=exportSettings; $('clear-settings').onclick=clearAllSettings;
    document.querySelectorAll('.logo-preset').forEach(btn=>btn.onclick=()=>setLogoPreset(btn.dataset.logo)); $('logo-file').onchange=loadLogo; $('logo-fit').onchange=e=>{const img=$('logo-preview').querySelector('img');if(img)img.style.objectFit=e.target.value}; $('logo-background').oninput=e=>$('logo-preview').style.background=e.target.value; $('logo-reset').onclick=()=>setLogoPreset('D');
    $('ticket-create').onclick=createTicket; $('lock-create').onclick=createLock; $('notification-clear').onclick=()=>{state.notifications=[];saveState();renderNotifications()};
    $('schedule-save').onclick=saveSchedule; renderWeekdays();
    $('tab-list').addEventListener('click',e=>{if(e.target.closest('.tab'))contextTab=e.target.closest('.tab').dataset.id}); document.addEventListener('click',e=>{if(!e.target.closest('#context-menu')&&!e.target.closest('.tab'))$('context-menu').hidden=true});
    $('context-menu').addEventListener('click',e=>{const action=e.target.closest('[data-action]')?.dataset.action;if(action)runContextAction(action)}); $('context-search').oninput=e=>filterElements('#context-menu>[role="menuitem"]',e.target.value,'context-search');
    $('managed-tab-list').addEventListener('click',e=>{const pin=e.target.closest('.managed-pin'),open=e.target.closest('.managed-open');if(pin)togglePin(pin.dataset.id);if(open)activateDestination(open.dataset.id)});
    $('palette-results').addEventListener('click',e=>{const result=e.target.closest('[data-target]');if(result){$('command-palette').close();activateDestination(result.dataset.target)}});
    $('pin-current').onclick=()=>togglePin(state.activeTab); $('bulk-tab-query').oninput=previewBulk; $('close-containing').onclick=()=>bulkClose(false); $('close-not-containing').onclick=()=>bulkClose(true);
  }
  function update(key,value,after){state[key]=value;saveState();after?.()}
  function openPalette(){$('command-palette').showModal();$('palette-search').value='';renderPalette('');setTimeout(()=>$('palette-search').focus(),0)}
  function selectSettingsTab(id){document.querySelectorAll('[data-settings-tab]').forEach(b=>b.setAttribute('aria-selected',String(b.dataset.settingsTab===id)));document.querySelectorAll('.settings-panel').forEach(p=>p.classList.toggle('active',p.dataset.panel===id))}
  function matchText(text,query,target){if(!query)return true;const config=regexState.get(target);if(config?.enabled){try{return new RegExp(config.pattern,config.flags).test(text)}catch{return false}}return text.toLocaleLowerCase().includes(query.toLocaleLowerCase())}
  function filterElements(selector,query,target){document.querySelectorAll(selector).forEach(el=>el.hidden=!matchText(el.dataset.search||el.textContent,query,target))}

  function openRegex(target){ regexTarget=target; const saved=regexState.get(target)||{pattern:'',flags:'iu'}; $('regex-target-label').textContent=`Attached to: ${target}`;$('regex-pattern').value=saved.pattern;$('regex-i').checked=saved.flags.includes('i');$('regex-m').checked=saved.flags.includes('m');$('regex-u').checked=saved.flags.includes('u');$('regex-dialog').showModal();previewRegex();setTimeout(()=>$('regex-pattern').focus(),0) }
  function insertRegex(value){const input=$('regex-pattern'),start=input.selectionStart,end=input.selectionEnd;input.value=input.value.slice(0,start)+value+input.value.slice(end);input.focus();input.setSelectionRange(start+value.length,start+value.length);previewRegex()}
  function regexConfig(){return{pattern:$('regex-pattern').value.slice(0,256),flags:`${$('regex-i').checked?'i':''}${$('regex-m').checked?'m':''}${$('regex-u').checked?'u':''}`}}
  function previewRegex(){const c=regexConfig();if(!c.pattern){$('regex-feedback').textContent='Enter a pattern.';return}try{const re=new RegExp(c.pattern,c.flags),matches=[...$('regex-sample').value.matchAll(new RegExp(re.source,re.flags.includes('g')?re.flags:`${re.flags}g`))];$('regex-feedback').textContent=`Valid JavaScript regular expression · ${matches.length} sample match${matches.length===1?'':'es'}.`}catch(error){$('regex-feedback').textContent=`Invalid pattern: ${error.message}`}}
  function applyRegex(){const c=regexConfig();try{new RegExp(c.pattern,c.flags)}catch{return}regexState.set(regexTarget,{...c,enabled:Boolean(c.pattern)});$('regex-dialog').close();const target=$(regexTarget);if(target)target.dispatchEvent(new Event('input'));notify('Regular expression applied',`The ${regexTarget} field now uses the JavaScript regular expression engine.`)}

  function openContext(event,id){event.preventDefault();contextTab=id;const menu=$('context-menu');menu.hidden=false;menu.style.left=`${Math.min(event.clientX,innerWidth-360)}px`;menu.style.top=`${Math.min(event.clientY,innerHeight-230)}px`;setTimeout(()=>$('context-search').focus(),0)}
  function runContextAction(action){if(action==='pin')togglePin(contextTab);if(action==='appearance'){activateDestination('appearance');notify('Appearance editor opened',`Edit the local presentation for ${DESTINATIONS.find(x=>x.id===contextTab)?.name||'this tab'}.`)}if(action==='lock'){activateDestination('security');$('lock-target').value='Settings';notify('Toy-lock surface opened','This local demonstration is not a security boundary.')} $('context-menu').hidden=true}
  function togglePin(id){state.pinned=state.pinned.includes(id)?state.pinned.filter(x=>x!==id):[...state.pinned,id];saveState();renderTabs();notify('Tab state changed',`${DESTINATIONS.find(x=>x.id===id)?.name} is now ${state.pinned.includes(id)?'pinned':'unpinned'}.`)}
  function previewBulk(){const q=$('bulk-tab-query').value.trim();const count=q?DESTINATIONS.filter(x=>!state.pinned.includes(x.id)&&x.name.toLowerCase().includes(q.toLowerCase())).length:0;$('bulk-preview').textContent=q?`${count} unpinned tab${count===1?'':'s'} contain this text. Pinned tabs remain protected.`:'Enter text to preview affected unpinned tabs.'}
  function bulkClose(inverse){const q=$('bulk-tab-query').value.trim().toLowerCase();if(!q){notify('Bulk close not run','Enter text before previewing or closing tabs.',true);return}const affected=DESTINATIONS.filter(x=>!state.pinned.includes(x.id)&&(inverse?!x.name.toLowerCase().includes(q):x.name.toLowerCase().includes(q))).map(x=>x.id);state.closed=[...new Set([...state.closed,...affected])];saveState();renderTabs();notify('Tabs closed',`${affected.length} unpinned tab${affected.length===1?'':'s'} closed. Reopen them from the tab manager.`)}

  async function loadVocabulary(event){const file=event.target.files[0];if(!file)return;if(file.size>65536){$('vocabulary-status').textContent='Rejected: file exceeds 64 KiB.';return}try{const text=await file.text();const parsed=parseJsonNoDuplicateKeys(text);if(parsed.version!==1||!Array.isArray(parsed.replacements)||parsed.replacements.length>256)throw new Error('Expected version 1 and no more than 256 replacements.');for(const entry of parsed.replacements){if(!entry||Object.keys(entry).sort().join(',')!=='from,to'||typeof entry.from!=='string'||typeof entry.to!=='string'||entry.from.length>128||entry.to.length>256)throw new Error('Each replacement must contain bounded string fields named from and to.')}localStorage.setItem('ding-pbx-vocabulary-cache',JSON.stringify(parsed));$('vocabulary-status').textContent=`Loaded ${parsed.replacements.length} local replacement${parsed.replacements.length===1?'':'s'}.`;notify('Personal vocabulary loaded','Validated data stays in this browser and is excluded from exports.')}catch(error){$('vocabulary-status').textContent=`Rejected: ${error.message}`;event.target.value=''}}
  function parseJsonNoDuplicateKeys(text){
    const parsed=JSON.parse(text), topKeys=Object.keys(parsed);
    if(/"(?:__proto__|prototype|constructor)"\s*:/.test(text))throw new Error('Unsafe object key.');
    for(const key of ['version','replacements'])if((text.match(new RegExp(`"${key}"\\s*:`, 'g'))||[]).length!==1)throw new Error('Duplicate or missing top-level key.');
    for(const objectText of text.match(/\{[^{}]*\}/g)||[]){const keys=[...objectText.matchAll(/"((?:\\.|[^"\\])*)"\s*:/g)].map(match=>JSON.parse(`"${match[1]}"`));if(new Set(keys).size!==keys.length)throw new Error('Duplicate keys are not accepted.');}
    if(topKeys.sort().join(',')!=='replacements,version')throw new Error('Unexpected top-level fields.');return parsed
  }
  function clearVocabulary(){localStorage.removeItem('ding-pbx-vocabulary-cache');$('vocabulary-file').value='';$('vocabulary-status').textContent='No file loaded; original wording is active.'}
  function exportSettings(){const redacted={...state,locks:state.locks.map(({target,created})=>({target,created,credential:'omitted'})),tickets:state.tickets.map(({id,category,status,created})=>({id,category,status,created,description:'omitted'}))};download('ding-pbx-site-settings.json',JSON.stringify({schemaVersion:1,encoding:'UTF-8',personalVocabulary:'omitted',settings:redacted},null,2),'application/json')}
  function download(name,text,type){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text],{type}));a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)}
  function clearAllSettings(){if(!confirm('Reset all local settings, notifications, toy locks, and tickets for this website?'))return;localStorage.removeItem(STORAGE_KEY);localStorage.removeItem('ding-pbx-vocabulary-cache');location.reload()}

  function setLogoPreset(value){$('logo-preview').innerHTML=`<span>${escapeHtml(value)}</span>`;$('brand-mark').textContent=value;$('logo-status').textContent='Using a shipped local preset.'}
  function loadLogo(event){const file=event.target.files[0];if(!file)return;if(file.size>2*1024*1024){$('logo-status').textContent='Rejected: image exceeds 2 MiB.';return}if(!['image/png','image/jpeg','image/webp','image/svg+xml'].includes(file.type)){$('logo-status').textContent='Rejected: unsupported image type.';return}const url=URL.createObjectURL(file),img=new Image();img.onload=()=>{if(img.naturalWidth*img.naturalHeight>16_000_000){$('logo-status').textContent='Rejected: decoded image exceeds 16 megapixels.';URL.revokeObjectURL(url);return}img.style.objectFit=$('logo-fit').value;$('logo-preview').replaceChildren(img);$('brand-mark').textContent='◈';$('logo-status').textContent=`Local preview ready at ${img.naturalWidth} × ${img.naturalHeight}. It is not uploaded or exported.`};img.onerror=()=>{$('logo-status').textContent='Rejected: image could not be decoded.';URL.revokeObjectURL(url)};img.src=url}

  async function createLock(){const pin=$('lock-pin').value;if(pin.length<4){$('lock-status').textContent='Enter at least four characters. The value is not described or echoed.';return}const data=new TextEncoder().encode(`${$('lock-target').value}:${pin}`),digest=await crypto.subtle.digest('SHA-256',data);state.locks.push({target:$('lock-target').value,digest:Array.from(new Uint8Array(digest),x=>x.toString(16).padStart(2,'0')).join(''),created:new Date().toISOString()});$('lock-pin').value='';saveState();$('lock-status').textContent=`Local toy lock recorded for ${$('lock-target').value}. Clear site data to recover.`}
  function createTicket(){const ticket={id:`LOCAL-${String(Date.now()).slice(-6)}`,category:$('ticket-category').value,description:$('ticket-description').value.trim(),status:'Resolved locally: clear this site’s browser data if reset is needed',created:new Date().toISOString()};state.tickets.unshift(ticket);saveState();$('ticket-description').value='';renderTickets();notify('Local ticket created',`${ticket.id} exists only in this browser. Nobody is reading it.`)}
  function renderWeekdays(){const names=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];$('weekday-list').innerHTML=names.map((name,i)=>`<label><input type="checkbox" value="${i}" ${state.schedule.days.includes(i)?'checked':''}>${name}</label>`).join('');$('schedule-enabled').checked=state.schedule.enabled}
  function saveSchedule(){state.schedule={enabled:$('schedule-enabled').checked,startDate:$('schedule-start-date').value,endDate:$('schedule-end-date').value,startTime:$('schedule-start-time').value,endTime:$('schedule-end-time').value,theme:$('schedule-theme').value,days:[...$('weekday-list').querySelectorAll(':checked')].map(x=>Number(x.value)),timezone:Intl.DateTimeFormat().resolvedOptions().timeZone};saveState();$('schedule-status').textContent=`Saved in ${state.schedule.timezone}. Cross-midnight windows continue into the following day; empty weekday selection matches no day.`;notify('Schedule saved','The local site schedule is persisted. External sources are not contacted by this static page.')}
  function notify(title,body,persistent=false){const item={title,body,time:Date.now()};state.notifications.unshift(item);state.notifications=state.notifications.slice(0,100);saveState();renderNotifications();const toast=document.createElement('div');toast.className='toast';toast.innerHTML=`<strong>${state.dialogEmoji?'ℹ️ ':''}${escapeHtml(title)}</strong><span>${escapeHtml(body)}</span>`;$('toast-region').append(toast);if(!persistent)setTimeout(()=>toast.remove(),6000)}
  function updateTimeAwareness(){if(!state.attention.time)return;const start=Number(sessionStorage.getItem('ding-session-start'))||Date.now();sessionStorage.setItem('ding-session-start',String(start));$('installer-status').textContent=`No verified release manifest exists yet. This site session has been open ${Math.floor((Date.now()-start)/60000)} minute(s).`}
  function reduceMotion(){return matchMedia('(prefers-reduced-motion: reduce)').matches||state.attention.low}
  init();
})();
