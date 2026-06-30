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
    rendererStatus: $("renderer-status"),
    rendererLabel: $("renderer-label"),
    rendererDetail: $("renderer-detail"),
    characterSelect: $("character-select"),
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
    importBundle: $("import-memory-bundle"),
    bundleFile: $("memory-bundle-file"),
    resetRuntime: $("reset-runtime"),
    portraitAction: $("portrait-action"),
    portraitFile: $("portrait-file"),
    modalBackdrop: $("modal-backdrop"),
    modalMessage: $("modal-message"),
    modalClose: $("modal-close"),
};

const SETTINGS_KEY = "persona_presence_settings_v2";
const WEB_USER_KEY = "persona_web_user_id_v1";
const DEFAULT_SETTINGS = {
    proactive: true,
    speakFirst: false,
    idleDelay: 45000,
};

const LK_SCOPE_IDS = {
    real_world: 1,
    cartridge_canon: 2,
    simulation_world: 3,
    actor_specific: 4,
    relationship_specific: 5,
    session_local: 6,
    private_character_belief: 7,
    private_belief: 7,
};

const LK_STATUS_IDS = {
    provisional: 1,
    confirmed: 2,
    corrected: 3,
    disputed: 4,
    deprecated: 5,
    cartridge_authored: 6,
    world_authored: 7,
    candidate: 8,
};

const LK_EDGE_IDS = {
    corrects: 1,
    contradicts: 2,
    supports: 3,
    derived_from: 4,
    taught_by: 5,
    belongs_to_scope: 6,
    evidenced_by: 7,
    used_in_response: 8,
    related_to_actor: 9,
    related_to_topic: 10,
};

const CHARACTER_CATALOG = {
    pretorius: { name: "Dr. Pretorius", path: "profiles/pretorius/pretorius.cart" },
    kiki:      { name: "Kiki",          path: "profiles/kiki/kiki.cart" },
    r0r1:      { name: "R0-R1",         path: "profiles/r0r1/r0r1.cart" },
    friendly:  { name: "Mira",          path: "profiles/friendly/friendly.cart" },
    rival:     { name: "Cassian Vale",  path: "profiles/rival/rival.cart" },
    quiet:     { name: "Eli Rowan",     path: "profiles/quiet/quiet.cart" },
    mentor:    { name: "Marin Hale",    path: "profiles/mentor/mentor.cart" },
};

let settings = loadSettings();
let idleTimer = null;
let idleProbeTurn = -1;
let idleRequestInFlight = false;
let loadInFlight = false;
let speakFirstAttempted = false;
let latestTurn = 0;
let latestState = null;
let manualProbeTurn = -1;
let sessionHasUserTurn = false;

function webUserId() {
    const saved = (localStorage.getItem(WEB_USER_KEY) || "").trim();
    return saved || "You";
}

async function ensureWebUser() {
    const user_id = webUserId();
    try {
        const r = await fetch("/set_user", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({ user_id }),
        });
        const data = await r.json();
        if (!r.ok || data.error || data.ok !== true)
            throw new Error(data.error || "set_user failed");
        return true;
    } catch (e) {
        console.error(e);
        showModal("The chat server is running, but the browser could not establish a local user identity. Restart the chat server if replies seem to use the wrong memory.");
        return false;
    }
}

function clampPct(n) {
    return Math.max(0, Math.min(100, Math.round(n)));
}

async function postJson(url, body) {
    const r = await fetch(url, {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(body || {}),
    });
    const data = await r.json();
    if (!r.ok || data.error || data.ok === false) {
        throw new Error(data.error || `request failed: ${url}`);
    }
    return data;
}

function evidenceRefToInt(text) {
    const s = String(text || "").trim();
    let h = 2166136261 >>> 0;
    for (let i = 0; i < s.length; ++i) {
        h ^= s.charCodeAt(i) & 0xff;
        h = Math.imul(h, 16777619) >>> 0;
    }
    return h >>> 0;
}

function mapNameId(map, value, fallback) {
    if (typeof value === "number" && Number.isFinite(value)) return value;
    const key = String(value || fallback || "").trim();
    return Object.prototype.hasOwnProperty.call(map, key) ? map[key] : map[fallback];
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

function slugFromState(s) {
    const raw = String((s && (s.profile_slug || s.name)) || "").toLowerCase();
    if (raw.includes("pretorius")) return "pretorius";
    if (raw.includes("kiki")) return "kiki";
    if (raw.includes("r0-r1") || raw.includes("r0r1")) return "r0r1";
    if (raw.includes("mira") || raw.includes("friendly")) return "friendly";
    if (raw.includes("cassian") || raw.includes("rival")) return "rival";
    if (raw.includes("eli") || raw.includes("quiet")) return "quiet";
    if (raw.includes("marin") || raw.includes("mentor")) return "mentor";
    return "";
}

function resetTranscript(message) {
    els.messages.replaceChildren();
    const hint = document.createElement("div");
    hint.className = "empty-hint";
    hint.textContent = message || "Start the conversation whenever you are ready. This is running locally in offline template mode unless you chose an optional renderer at launch.";
    els.messages.appendChild(hint);
}

function rendererStatusFromState(s) {
    const backend = String((s && s.renderer_backend) || "template").toLowerCase();
    const provider = String((s && s.renderer_provider) || "none").toLowerCase();
    const model = String((s && s.renderer_model) || "").trim();
    const mode = String((s && s.renderer_mode) || "").toLowerCase();

    if (mode === "ollama" || (backend === "slm" && provider === "ollama")) {
        return {
            cls: "renderer-online",
            label: "Ollama mode",
            detail: model ? `${model} connected locally` : "local model renderer active",
            subtitle: model ? `local Ollama session | ${model}` : "local Ollama session",
        };
    }
    if (mode === "api" || (backend === "slm" && provider === "api")) {
        return {
            cls: "renderer-api",
            label: "API mode",
            detail: model ? `${model} via API renderer` : "remote/API renderer active",
            subtitle: model ? `API renderer | ${model}` : "API renderer session",
        };
    }
    return {
        cls: "renderer-offline",
        label: "Offline mode",
        detail: "template renderer active",
        subtitle: "private local offline session",
    };
}

function syncTranscriptHintFromState(s) {
    const current = els.messages.querySelector(".empty-hint");
    if (!current) return;
    if (Number((s && s.turn_count) || 0) > 0) {
        resetTranscript(`Continuing the existing local ${s.name || "character"} session. Use "start fresh local session" if you want a clean runtime.`);
    } else {
        resetTranscript();
    }
}

function setBusy(busy, label) {
    loadInFlight = !!busy;
    els.input.disabled = !!busy;
    els.send.disabled = !!busy;
    els.promptCharacter.disabled = !!busy || idleRequestInFlight || manualProbeTurn === latestTurn;
    if (els.importBundle) els.importBundle.disabled = !!busy;
    if (els.resetRuntime) els.resetRuntime.disabled = !!busy;
    if (busy) setPresence(label || "switching", true);
    else if (latestState) setPresence(presenceFromState(latestState), false);
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
    if (!s) return "gathering a thought";
    const mood = Number(s.mood || 0);
    const obsession = Number(s.obsession_pressure || 0);
    const unresolved = Number(s.unresolved_count || 0);
    const sinceQuestion = Number(s.turns_since_question || 0);
    const wantAges = Array.isArray(s.want_ages) ? s.want_ages : [];
    const wantMax = wantAges.reduce((a, b) => Math.max(a, Number(b || 0)), 0);

    if (unresolved > 0) return "holding an unfinished thread";
    if (s.last_reply_had_question) return "waiting for your answer";
    if (wantMax > 6) return "wanting to steer the subject";
    if (obsession >= 700) return "holding a strong focus";
    if (sinceQuestion >= 4) return "looking for a better question";
    if (mood < -300) return "guarding the next reply";
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
    els.promptCharacter.disabled = loadInFlight || idleRequestInFlight || manualProbeTurn === latestTurn;
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
    const renderer = rendererStatusFromState(s);
    els.name.textContent    = s.name || "-";
    els.initial.textContent = (s.name || "?").charAt(0).toUpperCase();
    els.today.textContent   = s.today || "";
    els.chatTitle.textContent = s.name || "Chat";
    els.chatSubtitle.textContent = renderer.subtitle;
    els.mood.textContent    = s.mood;
    els.intent.textContent  = s.intent;
    els.mode.textContent    = s.rhetorical_mode;
    els.turn.textContent    = s.turn_count;
    els.intox.textContent   = s.intoxication;
    els.exhaust.textContent = s.exhaustion;
    els.delta.textContent   = s.voice_delta;
    els.disp.textContent    = s.disposition;
    latestTurn = Number(s.turn_count || 0);
    const slug = slugFromState(s);
    if (slug && els.characterSelect && els.characterSelect.value !== slug)
        els.characterSelect.value = slug;
    if (els.rendererStatus) {
        els.rendererStatus.classList.remove("renderer-offline", "renderer-online", "renderer-api");
        els.rendererStatus.classList.add(renderer.cls);
    }
    if (els.rendererLabel) els.rendererLabel.textContent = renderer.label;
    if (els.rendererDetail) els.rendererDetail.textContent = renderer.detail;
    setPresence(presenceFromState(s), false);
    updateInnerLife(s);
}

async function fetchState() {
    try {
        const r = await fetch("/state");
        if (r.ok) {
            const state = await r.json();
            setState(state);
            syncTranscriptHintFromState(state);
        }
    } catch (e) { console.error(e); }
}

async function loadCharacter(slug) {
    const entry = CHARACTER_CATALOG[slug];
    if (!entry || loadInFlight) return;
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = null;
    idleProbeTurn = -1;
    manualProbeTurn = -1;
    speakFirstAttempted = false;
    sessionHasUserTurn = false;
    setBusy(true, "loading character");
    resetTranscript(`Loading ${entry.name}...`);
    try {
        const r = await fetch("/load", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({ path: entry.path }),
        });
        const data = await r.json();
        if (!r.ok || data.error || data.ok !== true) {
            throw new Error(data.error || `load failed (${data.code ?? r.status})`);
        }
        await ensureWebUser();
        const stateResp = await fetch("/state");
        if (!stateResp.ok) throw new Error("state check failed after load");
        const state = await stateResp.json();
        const activeSlug = slugFromState(state);
        if (activeSlug && activeSlug !== slug)
            throw new Error(`host loaded ${state.name || activeSlug}, not ${entry.name}`);
        setState(state);
        loadPortrait();
        resetTranscript(`You are now talking with ${state.name || entry.name}.`);
    } catch (e) {
        resetTranscript("Character switch failed. The current session was not changed safely.");
        showModal(`Could not load ${entry.name}: ${e.message}`);
        await fetchState();
    } finally {
        setBusy(false);
        els.input.focus();
        maybeSpeakFirst();
        scheduleIdleProbe();
    }
}

async function resetRuntimeSession() {
    if (loadInFlight) return;
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = null;
    idleProbeTurn = -1;
    manualProbeTurn = -1;
    speakFirstAttempted = false;
    sessionHasUserTurn = false;
    setBusy(true, "starting fresh session");
    resetTranscript("Clearing local runtime memory for this cartridge...");
    try {
        await postJson("/reset_runtime", {});
        await ensureWebUser();
        const stateResp = await fetch("/state");
        if (!stateResp.ok) throw new Error("state check failed after reset");
        const state = await stateResp.json();
        setState(state);
        loadPortrait();
        resetTranscript(`Fresh local session started for ${state.name || "this character"}.`);
    } catch (e) {
        resetTranscript("Fresh-session reset failed. The existing runtime was left in place.");
        showModal(`Could not reset this local session: ${e.message}`);
        await fetchState();
    } finally {
        setBusy(false);
        els.input.focus();
        maybeSpeakFirst();
        scheduleIdleProbe();
    }
}

function scheduleIdleProbe() {
    if (idleTimer) clearTimeout(idleTimer);
    if (!settings.proactive) return;
    if (!sessionHasUserTurn && latestTurn > 0) return;
    const delay = Number(settings.idleDelay || DEFAULT_SETTINGS.idleDelay);
    idleTimer = setTimeout(() => requestIdleProbe({ allowFresh: false, meta: "quiet" }), delay);
}

async function requestIdleProbe(opts = {}) {
    const allowFresh = !!opts.allowFresh;
    const allowManual = !!opts.allowManual;
    const meta = opts.meta || "quiet";
    if (!settings.proactive || loadInFlight) return;
    if (idleRequestInFlight || els.send.disabled) {
        scheduleIdleProbe();
        return;
    }
    if (document.hidden || els.input.value.trim()) {
        scheduleIdleProbe();
        return;
    }
    if ((!allowFresh && latestTurn === 0)
        || (!allowFresh && !allowManual && !sessionHasUserTurn && latestTurn > 0)
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
    if (loadInFlight) return;
    sessionHasUserTurn = true;
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
        els.send.disabled = loadInFlight;
        els.input.disabled = loadInFlight;
        els.input.focus();
        scheduleIdleProbe();
    }
}

async function importHistoryBundle(file) {
    if (!file || loadInFlight) return;
    let bundle;
    let imported = {
        core: 0,
        episodic: 0,
        relationships: 0,
        openLoops: 0,
        learned: 0,
        edges: 0,
    };
    setBusy(true, "importing history");
    try {
        bundle = JSON.parse(await file.text());
        if (!bundle || bundle.bundle_version !== 1 || !bundle.records || typeof bundle.records !== "object")
            throw new Error("not a valid V6 bundle_version 1 file");

        const records = bundle.records;
        const lkMap = new Map();
        const learned = Array.isArray(records.learned_knowledge) ? records.learned_knowledge : [];

        for (const rec of Array.isArray(records.core_memories) ? records.core_memories : []) {
            const row = await postJson("/import_memory", {
                summary: rec.summary || rec.text || "",
                topic_key: rec.topic_key || rec.topic || "core",
                actor_name: rec.actor_name || rec.actor || "",
                salience: Number(rec.salience || 70),
                emotional_impact: Number(rec.emotional_impact || 0),
                is_core: 1,
                is_pinned: rec.pinned ? 1 : 0,
            });
            if (row.memory_id) imported.core++;
        }

        for (const rec of Array.isArray(records.episodic_memories) ? records.episodic_memories : []) {
            const row = await postJson("/import_memory", {
                summary: rec.summary || rec.text || "",
                topic_key: rec.topic_key || rec.topic || "episode",
                actor_name: rec.actor_name || rec.actor || "",
                salience: Number(rec.salience || 50),
                emotional_impact: Number(rec.emotional_impact || 0),
                is_core: 0,
                is_pinned: rec.pinned ? 1 : 0,
            });
            if (row.memory_id) imported.episodic++;
        }

        for (const rec of Array.isArray(records.relationships) ? records.relationships : []) {
            await postJson("/import_relationship", {
                actor_name: rec.actor_name || rec.actor || "",
                trust: Number(rec.trust || 500),
                threat: Number(rec.threat || 500),
                intimacy: Number(rec.intimacy || 0),
                resentment: Number(rec.resentment || 0),
                dependency: Number(rec.dependency || 0),
                obligation: Number(rec.obligation || 0),
                envy: Number(rec.envy || 0),
                admiration: Number(rec.admiration || 500),
                embarrassment: Number(rec.embarrassment || 0),
            });
            imported.relationships++;
        }

        for (const rec of Array.isArray(records.open_loops) ? records.open_loops : []) {
            const row = await postJson("/import_open_loop", {
                actor_name: rec.actor_name || rec.actor || "",
                topic_key: rec.topic_key || rec.topic || "",
                desired_speech_act: rec.desired_speech_act || rec.speech_act || "return_to",
                urgency: Number(rec.urgency || 500),
                shame_cost: Number(rec.shame_cost || 0),
                avoidance_pressure: Number(rec.avoidance_pressure || 0),
            });
            if (row.loop_id) imported.openLoops++;
        }

        for (let i = 0; i < learned.length; ++i) {
            const rec = learned[i];
            const importId = rec.import_id || `lk_${i + 1}`;
            const row = await postJson("/import_learned_knowledge", {
                topic_key: rec.topic_key || rec.topic || "knowledge",
                claim_text: rec.claim_text || rec.claim || "",
                scope: Number(rec.scope_id || mapNameId(LK_SCOPE_IDS, rec.scope, "real_world")),
                source_type: 7,
                source_tier: 5,
                status: Number(rec.status_id || mapNameId(LK_STATUS_IDS, rec.status, "candidate")),
                authority_rank: Math.min(60, Number(rec.authority_rank || 35)),
                confidence: Number(rec.confidence || 450),
                source_actor_name: rec.source_actor_name || rec.actor_name || "",
                correction_of_record_id: 0,
                evidence_ref: evidenceRefToInt(rec.evidence_ref || rec.evidence || ""),
                domain_tag: 0,
            });
            if (row.record_id) {
                lkMap.set(importId, row.record_id);
                imported.learned++;
            }
        }

        for (let i = 0; i < learned.length; ++i) {
            const rec = learned[i];
            const sourceId = lkMap.get(rec.import_id || `lk_${i + 1}`);
            const targetId = lkMap.get(rec.correction_of || "");
            if (!sourceId || !targetId) continue;
            const row = await postJson("/import_learned_edge", {
                source_record_id: sourceId,
                relation_type: 1,
                target_record_id: targetId,
                weight: 1000,
                confidence: 1000,
            });
            if (row.edge_id) imported.edges++;
        }

        for (const edge of Array.isArray(records.learned_knowledge_edges) ? records.learned_knowledge_edges : []) {
            const sourceId = lkMap.get(edge.source_import_id || edge.source || "");
            const targetId = lkMap.get(edge.target_import_id || edge.target || "");
            if (!sourceId || !targetId) continue;
            const row = await postJson("/import_learned_edge", {
                source_record_id: sourceId,
                relation_type: Number(edge.relation_type_id || mapNameId(LK_EDGE_IDS, edge.relation_type || edge.type, "supports")),
                target_record_id: targetId,
                weight: Number(edge.weight || 500),
                confidence: Number(edge.confidence || 500),
            });
            if (row.edge_id) imported.edges++;
        }

        await postJson("/save", {});
        await fetchState();
        showModal(`Imported ${imported.core + imported.episodic} memories, ${imported.learned} learned claims, ${imported.relationships} relationships, and ${imported.openLoops} open loops. Attachment bonds are not applied by the runtime yet.`);
    } catch (e) {
        try { await postJson("/discard_changes", {}); } catch (discardErr) { console.error(discardErr); }
        await fetchState();
        showModal(`History import failed and in-memory changes were discarded. ${e.message}`);
    } finally {
        if (els.bundleFile) els.bundleFile.value = "";
        setBusy(false);
        els.input.focus();
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

els.characterSelect.addEventListener("change", () => {
    loadCharacter(els.characterSelect.value);
});

els.attach.addEventListener("click", () => {
    showModal("Attachments require a renderer that supports file input through an API or multimodal local model. Offline template chat is still fully available.");
});

els.connectModel.addEventListener("click", () => {
    showModal("Local model rendering is optional. Start PersonaConsole with the optional Ollama launcher to test it. The default offline cartridge mode does not need a model.");
});

els.importBundle.addEventListener("click", () => {
    els.bundleFile.click();
});

els.resetRuntime.addEventListener("click", () => {
    resetRuntimeSession();
});

els.bundleFile.addEventListener("change", () => {
    const file = els.bundleFile.files && els.bundleFile.files[0];
    if (file) importHistoryBundle(file);
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
resetTranscript();
ensureWebUser().then(fetchState).then(() => {
    loadPortrait();
    maybeSpeakFirst();
});
scheduleIdleProbe();
