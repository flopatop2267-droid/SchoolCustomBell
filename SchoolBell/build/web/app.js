// app.js - frontend for the school bell server. Talks to the C++ backend over REST.
const $ = (id) => document.getElementById(id);

let cfg = null;        // local copy of the server configuration
let selected = null;   // profile currently shown in the schedule editor

const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// ---------- API helper: JSON by default, throws on HTTP errors
async function api(path, opts = {}) {
  const res = await fetch(path, {
    ...opts,
    headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) },
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}

function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(t._timer);
  t._timer = setTimeout(() => t.classList.remove('show'), 2500);
}

const fail = (err) => toast('Ошибка: ' + err.message);

// ---------- live status (polling every second; cpp-httplib has no WebSocket)
async function refreshState() {
  try {
    const s = await api('/api/state');
    $('clock').textContent = s.serverTime;
    $('now').textContent = s.playing ? `Звучит: ${s.nowPlaying}` : 'Тишина';
    $('status-dot').classList.toggle('on', s.playing);
    $('active-profile').textContent = s.activeProfile;
    $('track-count').textContent = s.trackCount;
  } catch {
    $('now').textContent = 'Нет связи с сервером';
  }
}

// ---------- load / render configuration
async function loadConfig() {
  cfg = await api('/api/config');
  $('duration').value = cfg.durationSec;
  $('fade').value = cfg.fadeSec;
  $('volume').value = Math.round(cfg.volume * 100);
  $('mode').value = cfg.playMode;
  $('folder').value = cfg.musicFolder;
  selected = cfg.activeProfile;
  renderProfiles();
}

const currentProfile = () => cfg.profiles.find((p) => p.name === selected);

function renderProfiles() {
  $('profile-select').innerHTML = cfg.profiles
    .map((p) => `<option value="${esc(p.name)}">${esc(p.name)}</option>`).join('');
  $('profile-select').value = selected;
  renderBells();
}

function renderBells() {
  const p = currentProfile();
  $('bells').innerHTML = p.bells.map((b, i) => `
    <tr>
      <td><input type="time" value="${esc(b.time)}" data-i="${i}" data-field="time"></td>
      <td><label><input type="checkbox" data-i="${i}" data-field="enabled" ${b.enabled ? 'checked' : ''}> вкл</label></td>
      <td><button class="ghost small" data-del="${i}">✕</button></td>
    </tr>`).join('') || '<tr><td>Звонков нет (режим тишины)</td></tr>';
}

// ---------- schedule editing
$('profile-select').addEventListener('change', (e) => {
  selected = e.target.value;
  renderBells();
});

['input', 'change'].forEach((evt) =>
  $('bells').addEventListener(evt, (e) => {
    const { i, field } = e.target.dataset;
    if (i === undefined) return;
    const bell = currentProfile().bells[+i];
    bell[field] = field === 'enabled' ? e.target.checked : e.target.value;
  }));

$('bells').addEventListener('click', (e) => {
  const idx = e.target.dataset.del;
  if (idx === undefined) return;
  currentProfile().bells.splice(+idx, 1);
  renderBells();
});

$('btn-add-bell').onclick = () => {
  currentProfile().bells.push({ time: '12:00', enabled: true });
  renderBells();
};

$('btn-new-profile').onclick = () => {
  const name = prompt('Название режима (например, «Сокращённый день»):');
  if (!name || cfg.profiles.some((p) => p.name === name)) return;
  cfg.profiles.push({ name, bells: [] });
  selected = name;
  renderProfiles();
  toast('Режим добавлен. Не забудьте сохранить.');
};

$('btn-activate').onclick = async () => {
  try {
    await saveAll();   // the server must know the profile before it can be activated
    await api('/api/profile', { method: 'POST', body: JSON.stringify({ name: selected }) });
    cfg.activeProfile = selected;
    toast(`Активный режим: ${selected}`);
    refreshState();
  } catch (e) { fail(e); }
};

// ---------- saving
function readForm() {
  cfg.durationSec = +$('duration').value;
  cfg.fadeSec = +$('fade').value;
  cfg.volume = +$('volume').value / 100;
  cfg.playMode = $('mode').value;
  cfg.musicFolder = $('folder').value.trim() || 'music';
  cfg.profiles.forEach((p) => p.bells.sort((a, b) => a.time.localeCompare(b.time)));
}

async function saveAll() {
  readForm();
  await api('/api/config', { method: 'PUT', body: JSON.stringify(cfg) });
  renderBells();
  loadTracks();
}

['btn-save-schedule', 'btn-save-settings'].forEach((id) => {
  $(id).onclick = () => saveAll().then(() => toast('Сохранено')).catch(fail);
});

// ---------- manual controls
$('btn-test').onclick = () =>
  api('/api/play', { method: 'POST' }).then(() => toast('Звонок!')).catch(fail);

$('btn-stop').onclick = () =>
  api('/api/stop', { method: 'POST' }).then(() => toast('Остановлено')).catch(fail);

// ---------- playlist
async function loadTracks() {
  try {
    const { tracks } = await api('/api/tracks');
    $('track-list').innerHTML =
      tracks.map((t) => `<li>${esc(t)}</li>`).join('') || '<li>Файлов пока нет</li>';
  } catch (e) { fail(e); }
}

$('upload').addEventListener('change', async (e) => {
  for (const file of e.target.files) {
    try {
      await api(`/api/upload?name=${encodeURIComponent(file.name)}`, {
        method: 'POST',
        body: file,
        headers: { 'Content-Type': 'application/octet-stream' },
      });
      toast(`Загружено: ${file.name}`);
    } catch (err) { fail(err); }
  }
  e.target.value = '';
  loadTracks();
  refreshState();
});

// ---------- start up
(async function init() {
  try { await loadConfig(); } catch (e) { fail(e); }
  loadTracks();
  refreshState();
  setInterval(refreshState, 1000);
})();
