#!/usr/bin/env node
/* forge_parsers_test.js — verify each chat-log parser handles synthetic
 * fixtures shaped like the real export formats.  No persona_host needed
 * — this is a pure-JS unit test for the parser plumbing.
 */
const fs = require('fs');
const path = require('path');

const FORGE = path.join(__dirname, '..', 'CartridgeForge', 'forge.html');
const html = fs.readFileSync(FORGE, 'utf-8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
let js = m[1];

const STUB = new Proxy({}, {
  get(t, p){
    if (p === 'addEventListener') return () => {};
    if (p === 'classList')        return { add(){}, remove(){}, toggle(){} };
    if (p === 'appendChild')      return () => {};
    if (p === 'querySelectorAll') return () => [];
    if (p === 'querySelector')    return () => STUB;
    if (p === 'createElement')    return () => STUB;
    if (p === 'dataset')          return {};
    if (p === 'style')            return {};
    if (p === 'value' || p === 'textContent' || p === 'innerHTML') return '';
    if (p === 'hidden' || p === 'checked')                          return false;
    if (typeof p === 'string' && p.startsWith('on'))                return null;
    return STUB;
  },
  set(){ return true; }
});
global.document = {
  getElementById: () => STUB,
  querySelectorAll: () => [],
  querySelector:    () => STUB,
  createElement:    () => STUB,
};
global.window = { scrollTo(){}, addEventListener(){} };
global.FileReader = class { readAsText(){} };
global.Blob = class { constructor(b){ this.b = b; } };
global.URL = { createObjectURL(){ return ''; }, revokeObjectURL(){} };
global.TextEncoder = require('util').TextEncoder;
global.performance = { now: () => Date.now() };
global.confirm = () => false;
global.alert   = () => {};

const exposeNames = [
  'parseChatGPT','parseJSONL','parseSillyTavern',
  'parseClaudeExport','parseGemini','parseCharacterAI',
  'autoDetectParse',
];
js += `\n;module.exports = { ${exposeNames.map(n => n + ':typeof ' + n + '!=="undefined"?' + n + ':null').join(',')} };`;
const moduleObj = { exports: {} };
new Function('module', js).call(global, moduleObj);
const P = moduleObj.exports;

let fails = 0;
function check(name, cond, detail){
  if (cond) console.log(`  ok · ${name}`);
  else { console.error(`  FAIL · ${name}` + (detail ? '  ' + detail : '')); fails++; }
}

/* ----- Claude / Anthropic export ----- */
console.log('Claude export:');
{
  const fx = [{
    uuid: 'abc', name: 'Test',
    chat_messages: [
      { sender: 'human',     text: 'hello from a user' },
      { sender: 'assistant', text: 'reply from claude' },
      { sender: 'human',     content: [{ type:'text', text: 'second user msg' }] },
      { sender: 'assistant', content: 'plain string content path' },
    ]
  }];
  const utts = P.parseClaudeExport(fx);
  check('handles 4-msg fixture', utts.length === 4, `got ${utts.length}`);
  check('role mapping human→user', utts[0].role === 'user');
  check('role mapping assistant→assistant', utts[1].role === 'assistant');
  check('content array text extraction', utts[2].text.includes('second user msg'));
  check('content string fallback', utts[3].text === 'plain string content path');
  const auto = P.autoDetectParse('conversations.json', JSON.stringify(fx));
  check('autoDetect routes to Claude', auto.length === 4);
}

/* ----- Gemini API history ----- */
console.log('Gemini API history:');
{
  const fx = [
    { role: 'user',  parts: [{ text: 'what is entropy' }] },
    { role: 'model', parts: [{ text: 'entropy is a measure of disorder' }] },
    { role: 'user',  parts: [{ text: 'tell me more' }] },
    { role: 'model', parts: [{ text: 'it tends to increase' }] },
  ];
  const utts = P.parseGemini(fx);
  check('handles 4-msg fixture', utts.length === 4);
  check('role mapping model→assistant',
        utts[1].role === 'assistant' && utts[1].text.includes('disorder'));
  const auto = P.autoDetectParse('history.json', JSON.stringify(fx));
  check('autoDetect routes to Gemini', auto.length === 4 && auto[0].text.includes('entropy'));
}

/* ----- Gemini Takeout My Activity ----- */
console.log('Gemini Takeout activity:');
{
  const fx = [
    { header: 'Gemini', title: 'Said hello there gemini',
      subtitles: [{ name: 'I am a model trained by Google to help with questions and tasks' }] },
    { header: 'Gemini', title: 'Asked about photosynthesis',
      subtitles: [{ name: 'Photosynthesis is the process by which plants convert sunlight into chemical energy' }] },
  ];
  const utts = P.parseGemini(fx);
  check('extracts user titles + assistant subtitles', utts.length === 4, `got ${utts.length}`);
  check('user lines from titles', utts[0].role === 'user' && utts[0].text.includes('hello'));
  check('assistant lines from subtitles', utts[1].role === 'assistant' && utts[1].text.includes('Google'));
  const auto = P.autoDetectParse('MyActivity.json', JSON.stringify(fx));
  check('autoDetect routes to Gemini Takeout', auto.length === 4);
}

/* ----- character.ai chats wrapper ----- */
console.log('character.ai chats wrapper:');
{
  const fx = {
    chats: [
      { history: [
        { src: 'user', text: 'hey there' },
        { src: 'char', text: 'oh hi friend' },
        { src: 'user', text: 'how are you' },
        { src: 'char', text: 'a little tired but glad to see you' },
      ]}
    ]
  };
  const utts = P.parseCharacterAI(fx);
  check('handles 4-msg fixture', utts.length === 4);
  check('char → assistant', utts[1].role === 'assistant' && utts[1].text.includes('hi friend'));
  const auto = P.autoDetectParse('cai_export.json', JSON.stringify(fx));
  check('autoDetect routes to character.ai', auto.length === 4);
}

/* ----- character.ai flat list ----- */
console.log('character.ai flat list:');
{
  const fx = [
    { src: 'user', msg: 'hi' },
    { src: 'char', msg: 'hello, my dear' },
  ];
  const utts = P.parseCharacterAI(fx);
  check('handles 2-msg fixture', utts.length === 2);
  check('msg field as text', utts[1].text === 'hello, my dear');
  const auto = P.autoDetectParse('flat.json', JSON.stringify(fx));
  check('autoDetect routes to character.ai', auto.length === 2);
}

/* ----- regression: ChatGPT still routes correctly ----- */
console.log('ChatGPT regression:');
{
  const fx = [{
    mapping: {
      n1: { message: { author: { role: 'user' },      content: { parts: ['hello'] } } },
      n2: { message: { author: { role: 'assistant' }, content: { parts: ['hi there'] } } },
    }
  }];
  const auto = P.autoDetectParse('conversations.json', JSON.stringify(fx));
  check('ChatGPT still detected', auto.length === 2 && auto.some(u => u.text === 'hi there'));
}

if (fails){
  console.error(`--- ${fails} parser check(s) FAILED ---`);
  process.exit(1);
}
console.log('--- OK: all parser fixtures pass ---');
