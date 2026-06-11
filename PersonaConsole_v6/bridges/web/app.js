/* app.js -- chat-first UI for the deterministic PersonaConsole host. */

const $ = (id) => document.getElementById(id);

const els = {
    messages:  $("messages"),
    form:      $("input-form"),
    input:     $("input"),
    send:      $("send"),
    portrait:  $("portrait"),
    portraitImg: $("portrait-img"),
    name:      $("character-name"),
    initial:   $("portrait-initial"),
    today:     $("today-label"),
    mood:      $("mood"),
    intent:    $("intent"),
    mode:      $("mode"),
    turn:      $("turn"),
    intox:     $("intox"),
    exhaust:   $("exhaust"),
    delta:     $("voice-delta"),
    disp:      $("disp"),
    presence:  $("presence"),
    proactive: $("proactive-enabled"),
    speakFirst:$("speak-first"),
    idleDelay: $("idle-delay"),
    thought:   $("current-thought"),
    needFocus: $("need-focus"),
    needEnergy:$("need-energy"),
    needRapport:$("need-rapport"),
    promptCharacter: $("prompt-character"),
    chatTitle: $("chat-title"),
    chatSubtitle: $("chat-subtitle"),
    attach: $("attach-button"),
    connectModel: $("connect-model"),
    portraitAction: $("portrait-action"),
    portraitFile: $("portrait-file"),
    modalBackdrop: $("modal-backdrop"),
    modalMessage: $("modal-message"),
    modalClose: $("modal-close"),
};

const SETTINGS_KEY = "persona_presence_settings_v1";
const DEFAULT_SETTINGS = {
    proactive: true,
    speakFirst: true,
    idleDelay: 45000,
};

let settings = loadSettings();
let idleTimer = null;
let idleProbeTurn = -1;
let idleRequestInFlight = false;
let speakFirstAttempted = false;
let latestTurn = 0;
let latestState = null;
let manualProbeTurn = -1;

function clampPct(n) {
    return Math.max(0, Math.min(100, Math.round(n)));
}

function loadSettings() {
    try {
        const raw = localStorage.getItem(SETTINGS_KEY);
        return raw ? Object.assign({}, DEFAULT_SETTINGS, JSON.parse(raw))
                   : {...DEFAULT_SETTINGS};
    } catch (e) {
        return {...DEFAULT_SETTINGS};
    }
}

function saveSettings() {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
}

function applySettingsToControls() {
    els.proactive.checked = !!settings.proactive;
    els.speakFirst.checked = !!settings.speakFirst;
    els.idleDelay.value = String(settings.idleDelay);
}

function setPresence(text, active) {
    els.presence.textContent = text;
    els.presence.classList.toggle("active", !!active);
}

function presenceFromState(s) {
    if (!s) return "waiting";
    if (Number(s.exhaustion || 0) >= 700) return "tired";
    if (Number(s.obsession_pressure || 0) >= 650) return "preoccupied";
    if (s.last_reply_had_question) return "waiting on you";
    if (s.intent === "initiate" || s.intent === "probe") return "restless";
    if (Number(s.mood || 0) < -250) return "guarded";
    if (Number(s.mood || 0) > 250) return "bright";
    return "present";
}

function thoughtFromState(s) {
    if (!s) return "gathering himself";
    const mood = Number(s.mood || 0);
    const obsession = Number(s.obsession_pressure || 0);
    const unresolved = Number(s.unresolved_count || 0);
    const sinceQuestion = Number(s.turns_since_question || 0);
    const wantAges = Array.isArray(s.want_ages) ? s.want_ages : [];
    const wantMax = wantAges.reduce((a, b) => Math.max(a, Number(b || 0)), 0);

    if (unresolved > 0) return "holding an unfinished thread";
    if (s.last_reply_had_question) return "waiting for your answer";
    if (wantMax > 6) return "wanting to steer the subject";
    if (obsession >= 700) return "circling a private fixation";
    if (sinceQuestion >= 4) return "looking for a better question";
    if (mood < -300) return "nursing an irritation";
    if (mood > 300) return "pleased with the current direction";
    return "turning something over";
}

function updateInnerLife(s) {
    if (!s) return;
    const focus = clampPct(Number(s.obsession_pressure || 0) / 10);
    const energy = clampPct(100 - Number(s.exhaustion || 0) / 10);
    const rapport = clampPct((Number(s.disposition || 500) - 400) / 4);
    if (els.needFocus) els.needFocus.style.width = `${focus}%`;
    if (els.needEnergy) els.needEnergy.style.width = `${energy}%`;
    if (els.needRapport) els.needRapport.style.width = `${rapport}%`;
    if (els.thought) els.thought.textContent = thoughtFromState(s);
    els.promptCharacter.disabled = idleRequestInFlight || manualProbeTurn === latestTurn;
}

/* Try to fetch the character's portrait. On 404, keep the initial-letter
 * placeholder visible. */
function loadPortrait() {
    const url = "/portrait?ts=" + Date.now();
    const probe = new Image();
    probe.onload = () => {
        els.portraitImg.src = url;
        els.portraitImg.classList.add("loaded");
        els.initial.style.display = "none";
    };
    probe.onerror = () => {
        els.portraitImg.classList.remove("loaded");
        els.initial.style.display = "";
    };
    probe.src = url;
}

function addMessage(role, text, meta) {
    const hint = document.querySelector(".empty-hint");
    if (hint) hint.remove();
    const div = document.createElement("div");
    div.className = `msg ${role}`;
    div.textContent = text;
    if (meta) {
        const m = document.createElement("div");
        m.className = "meta";
        m.textContent = meta;
        div.appendChild(m);
    }
    els.messages.appendChild(div);
    els.messages.scrollTop = els.messages.scrollHeight;
}

function setState(s) {
    latestState = s;
    els.name.textContent    = s.name || "-";
    els.initial.textContent = (s.name || "?").charAt(0).toUpperCase();
    els.today.textContent   = s.today || "";
    els.chatTitle.textContent = s.name || "Chat";
    els.chatSubtitle.textContent = "private local session";
    els.mood.textContent    = s.mood;
    els.intent.textContent  = s.intent;
    els.mode.textContent    = s.rhetorical_mode;
    els.turn.textContent    = s.turn_count;
    els.intox.textContent   = s.intoxication;
    els.exhaust.textContent = s.exhaustion;
    els.delta.textContent   = s.voice_delta;
    els.disp.textContent    = s.disposition;
    latestTurn = Number(s.turn_count || 0);
    setPresence(presenceFromState(s), false);
    updateInnerLife(s);
}

async function fetchState() {
    try {
        const r = await fetch("/state");
        if (r.ok) setState(await r.json());
    } catch (e) { console.error(e); }
}

function scheduleIdleProbe() {
    if (idleTimer) clearTimeout(idleTimer);
    if (!settings.proactive) return;
    const delay = Number(settings.idleDelay || DEFAULT_SETTINGS.idleDelay);
    idleTimer = setTimeout(() => requestIdleProbe({ allowFresh: false, meta: "quiet" }), delay);
}

async function requestIdleProbe(opts = {}) {
    const allowFresh = !!opts.allowFresh;
    const allowManual = !!opts.allowManual;
    const meta = opts.meta || "quiet";
    if (!settings.proactive) return;
    if (idleRequestInFlight || els.send.disabled) {
        scheduleIdleProbe();
        return;
    }
    if (document.hidden || els.input.value.trim()) {
        scheduleIdleProbe();
        return;
    }
    if ((!allowFresh && latestTurn === 0)
        || (!allowManual && idleProbeTurn === latestTurn)
        || (allowManual && manualProbeTurn === latestTurn)) {
        scheduleIdleProbe();
        return;
    }
    idleRequestInFlight = true;
    els.promptCharacter.disabled = true;
    setPresence("thinking", true);
    try {
        const r = await fetch("/idle_probe", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: "{}",
        });
        const data = await r.json();
        if (data.state) setState(data.state);
        if (data.reply && idleProbeTurn !== latestTurn) {
            if (allowManual) manualProbeTurn = latestTurn;
            else idleProbeTurn = latestTurn;
            addMessage("char idle", data.reply, meta);
        }
    } catch (e) {
        console.error(e);
    } finally {
        idleRequestInFlight = false;
        updateInnerLife(latestState);
        scheduleIdleProbe();
    }
}

function maybeSpeakFirst() {
    if (speakFirstAttempted || !settings.proactive || !settings.speakFirst) return;
    speakFirstAttempted = true;
    setTimeout(() => {
        if (!settings.proactive || !settings.speakFirst) return;
        if (latestTurn !== 0 || els.input.value.trim() || document.hidden) return;
        requestIdleProbe({ allowFresh: true, meta: "first move" });
    }, 1800);
}

async function sendMessage(text) {
    addMessage("user", text);
    scheduleIdleProbe();
    els.send.disabled = true;
    els.input.value = "";
    els.portrait.classList.add("speaking");
    setPresence("listening", true);
    try {
        const r = await fetch("/chat", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({ text }),
        });
        const data = await r.json();
        if (data.error) {
            addMessage("char", `[error] ${data.error}`);
        } else {
            const meta = data.state
                ? `intent: ${data.state.intent} | mode: ${data.state.rhetorical_mode} | mood: ${data.state.mood}`
                : "";
            if (!data.pause) addMessage("char", data.reply, meta);
            if (data.state) setState(data.state);
            idleProbeTurn = -1;
            manualProbeTurn = -1;
        }
    } catch (e) {
        addMessage("char", `[network error] ${e.message}`);
    } finally {
        els.portrait.classList.remove("speaking");
        els.send.disabled = false;
        els.input.focus();
        scheduleIdleProbe();
    }
}

function showModal(message) {
    els.modalMessage.textContent = message;
    els.modalBackdrop.hidden = false;
}

function hideModal() {
    els.modalBackdrop.hidden = true;
}

els.form.addEventListener("submit", (e) => {
    e.preventDefault();
    const text = els.input.value.trim();
    if (text) sendMessage(text);
});

els.proactive.addEventListener("change", () => {
    settings.proactive = els.proactive.checked;
    saveSettings();
    if (settings.proactive) {
        maybeSpeakFirst();
        scheduleIdleProbe();
    } else {
        if (idleTimer) clearTimeout(idleTimer);
        idleTimer = null;
        setPresence(presenceFromState(latestState), false);
    }
});

els.speakFirst.addEventListener("change", () => {
    settings.speakFirst = els.speakFirst.checked;
    saveSettings();
    maybeSpeakFirst();
});

els.idleDelay.addEventListener("change", () => {
    settings.idleDelay = Number(els.idleDelay.value || DEFAULT_SETTINGS.idleDelay);
    saveSettings();
    scheduleIdleProbe();
});

els.promptCharacter.addEventListener("click", () => {
    requestIdleProbe({ allowFresh: true, allowManual: true, meta: "prompted" });
});

els.attach.addEventListener("click", () => {
    showModal("Attachments require a renderer that supports file input through an API or multimodal local model. Offline template chat is still fully available.");
});

els.connectModel.addEventListener("click", () => {
    showModal("Local model rendering is optional. Start PersonaConsole with the optional Ollama launcher to test it. The default offline cartridge mode does not need a model.");
});

els.portraitAction.addEventListener("click", () => {
    els.portraitFile.click();
});

els.portraitFile.addEventListener("change", () => {
    const file = els.portraitFile.files && els.portraitFile.files[0];
    if (!file) return;
    const url = URL.createObjectURL(file);
    els.portraitImg.src = url;
    els.portraitImg.classList.add("loaded");
    els.initial.style.display = "none";
    showModal("Portrait preview loaded for this browser session. Cartridge-bundled portraits will be supported through the authoring flow.");
});

els.modalClose.addEventListener("click", hideModal);
els.modalBackdrop.addEventListener("click", (e) => {
    if (e.target === els.modalBackdrop) hideModal();
});

applySettingsToControls();
const hint = document.createElement("div");
hint.className = "empty-hint";
hint.textContent = "Start the conversation whenever you are ready. This is running locally in offline template mode unless you chose an optional renderer at launch.";
els.messages.appendChild(hint);
fetchState().then(maybeSpeakFirst);
loadPortrait();
scheduleIdleProbe();
