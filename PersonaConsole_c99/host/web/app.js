/* app.js — minimal chat UI. */

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
};

/* Try to fetch the character's portrait.  On success, fade in the image
 * and hide the initial-letter placeholder.  On 404 (no portrait file in
 * the cartridge directory), keep the placeholder visible. */
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
    els.name.textContent    = s.name || "—";
    els.initial.textContent = (s.name || "?").charAt(0).toUpperCase();
    els.today.textContent   = s.today || "";
    els.mood.textContent    = s.mood;
    els.intent.textContent  = s.intent;
    els.mode.textContent    = s.rhetorical_mode;
    els.turn.textContent    = s.turn_count;
    els.intox.textContent   = s.intoxication;
    els.exhaust.textContent = s.exhaustion;
    els.delta.textContent   = s.voice_delta;
    els.disp.textContent    = s.disposition;
}

async function fetchState() {
    try {
        const r = await fetch("/state");
        if (r.ok) setState(await r.json());
    } catch (e) { console.error(e); }
}

async function sendMessage(text) {
    addMessage("user", text);
    els.send.disabled = true;
    els.input.value = "";
    els.portrait.classList.add("speaking");
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
                ? `intent: ${data.state.intent} · mode: ${data.state.rhetorical_mode} · mood: ${data.state.mood}`
                : "";
            addMessage("char", data.reply, meta);
            if (data.state) setState(data.state);
        }
    } catch (e) {
        addMessage("char", `[network error] ${e.message}`);
    } finally {
        els.portrait.classList.remove("speaking");
        els.send.disabled = false;
        els.input.focus();
    }
}

els.form.addEventListener("submit", (e) => {
    e.preventDefault();
    const text = els.input.value.trim();
    if (text) sendMessage(text);
});

fetchState();
loadPortrait();
