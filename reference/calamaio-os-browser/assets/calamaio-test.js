/* Calamaio OS · motore dei banchi TEST (simulatore dei pannelli e-ink)
 * Generato da CalamaioTest.dc.html: template + componente. */
(function () {
'use strict';
const template = String.raw`<div style="width: 1360px; height: 840px; box-sizing: border-box; padding: 30px 34px; background: #E7EAEE; color: #14181C; font-family: 'IBM Plex Sans', system-ui, sans-serif; display: flex; gap: 26px;">

  <div style="flex: 1 1 0; min-width: 0; display: flex; flex-direction: column; gap: 14px;">
    <div style="display: flex; flex-direction: column; gap: 4px;">
      <p style="margin: 0; font: 500 12px/1.4 'IBM Plex Mono', monospace; letter-spacing: 0.09em; text-transform: uppercase; color: #56606B;">TEST · {{clsLabel}}</p>
      <h1 style="margin: 0; font: 600 34px/1.1 'Chakra Petch', sans-serif;">{{devName}} {{devSize}}</h1>
      <p style="margin: 0; font: 400 14px/1.5 'IBM Plex Mono', monospace; color: #56606B;">{{spec}}</p>
    </div>

    <div style="flex: 1 1 0; min-height: 0; display: flex; align-items: center; justify-content: center; background: #DCE0E5; border-radius: 14px; padding: 16px;">

      <sc-if value="{{isAcrylic}}" hint-placeholder-val="{{true}}">
        <div style="display: flex; background: rgba(214, 226, 232, 0.6); border: 1px solid #B8C6CE; border-radius: 18px; padding: 12px; box-shadow: 0 18px 36px -24px rgba(0, 0, 0, 0.55);">
          <div style="display: flex; align-items: center; gap: 16px; background: #1A1C21; border: 1px solid #30343B; border-radius: 8px; padding: 12px;">
            <div style="background: #ECEDE8; padding: 6px; border-radius: 2px;">
              <canvas ref="{{setCanvas}}" data-cal="{{uid}}" width="{{devW}}" height="{{devH}}" onClick="{{tap}}" role="img" aria-label="Schermo e-ink simulato" style="display: block; width: {{cssW}}px; height: {{cssH}}px; image-rendering: {{rendering}}; background: #D6D7CF; cursor: {{cursor}};"></canvas>
            </div>
            <div style="display: flex; flex-direction: column; align-items: center; gap: 8px;">
              <span style="font: 600 10px 'IBM Plex Mono', monospace; letter-spacing: 0.06em; color: #D9DCE0;">{{keyName}}</span>
              <button type="button" aria-label="Tasto {{keyName}}" onPointerDown="{{k1.down}}" onPointerUp="{{k1.up}}" onPointerLeave="{{k1.leave}}" onKeyDown="{{k1.kd}}" onKeyUp="{{k1.ku}}" onClick="{{k1.click}}" onContextMenu="{{noCtx}}" style="width: 42px; height: 42px; border-radius: 50%; border: 0; cursor: pointer; touch-action: none; user-select: none; background: radial-gradient(circle at 40% 35%, #F0F1F3, #C9CCD1 60%, #9DA2A9); box-shadow: {{k1.sh}}; transform: {{k1.tf}};"></button>
              <span style="width: 16px; height: 16px; border-radius: 50%; background: #7C828A;"></span>
              <span style="font: 600 10px 'IBM Plex Mono', monospace; letter-spacing: 0.06em; color: #D9DCE0;">RST</span>
            </div>
          </div>
        </div>
      </sc-if>

      <sc-if value="{{isPcb}}" hint-placeholder-val="{{false}}">
        <div style="display: flex; align-items: center; gap: 16px; background: #16181D; border: 1px solid #2C3038; border-radius: 10px; padding: 14px; box-shadow: 0 18px 36px -24px rgba(0, 0, 0, 0.6);">
          <div style="background: #F1F1EE; padding: 5px; border-radius: 3px;">
            <canvas ref="{{setCanvas}}" data-cal="{{uid}}" width="{{devW}}" height="{{devH}}" onClick="{{tap}}" role="img" aria-label="Schermo e-ink simulato" style="display: block; width: {{cssW}}px; height: {{cssH}}px; image-rendering: {{rendering}}; background: #D6D7CF; cursor: {{cursor}};"></canvas>
          </div>
          <div style="display: flex; flex-direction: column; align-items: center; gap: 8px;">
            <span style="font: 600 10px 'IBM Plex Mono', monospace; letter-spacing: 0.06em; color: #D9DCE0;">{{keyName}}</span>
            <button type="button" aria-label="Tasto {{keyName}}" onPointerDown="{{k1.down}}" onPointerUp="{{k1.up}}" onPointerLeave="{{k1.leave}}" onKeyDown="{{k1.kd}}" onKeyUp="{{k1.ku}}" onClick="{{k1.click}}" onContextMenu="{{noCtx}}" style="width: 38px; height: 38px; border-radius: 6px; border: 1px solid #0B0C0E; cursor: pointer; touch-action: none; user-select: none; background: radial-gradient(circle at 50% 45%, #2A2D33 0 9px, #E2E4E7 10px); box-shadow: {{k1.sh}}; transform: {{k1.tf}};"></button>
            <span style="width: 14px; height: 14px; border-radius: 3px; background: #7C828A;"></span>
            <span style="font: 600 10px 'IBM Plex Mono', monospace; letter-spacing: 0.06em; color: #D9DCE0;">RST</span>
          </div>
        </div>
      </sc-if>

      <sc-if value="{{isCrow}}" hint-placeholder-val="{{false}}">
        <div style="display: flex; align-items: center; gap: 22px; background: linear-gradient(160deg, #EEF0EC, #E1E4DF); border: 1px solid #C3C8C1; border-radius: 20px; padding: 22px; box-shadow: 0 22px 44px -26px rgba(0, 0, 0, 0.6);">
          <div style="background: #111214; padding: 9px; border-radius: 6px;">
            <canvas ref="{{setCanvas}}" data-cal="{{uid}}" width="{{devW}}" height="{{devH}}" onClick="{{tap}}" role="img" aria-label="Schermo e-ink simulato" style="display: block; width: {{cssW}}px; height: {{cssH}}px; image-rendering: {{rendering}}; background: #D6D7CF; cursor: {{cursor}};"></canvas>
          </div>
          <div style="display: flex; flex-direction: column; align-items: center; gap: 14px; width: 66px;">
            <button type="button" aria-label="Tasto MENU" onPointerDown="{{kMenu.down}}" onPointerUp="{{kMenu.up}}" onPointerLeave="{{kMenu.leave}}" onKeyDown="{{kMenu.kd}}" onKeyUp="{{kMenu.ku}}" onClick="{{kMenu.click}}" onContextMenu="{{noCtx}}" style="width: 100%; padding: 11px 0; border: 0; border-radius: 7px; cursor: pointer; touch-action: none; user-select: none; background: #2A2D31; color: #F2F3F1; font: 700 11px/1 'Chakra Petch', sans-serif; letter-spacing: 0.06em; box-shadow: {{kMenu.sh}}; transform: {{kMenu.tf}};">MENU</button>
            <div style="display: flex; flex-direction: column; width: 54px; border-radius: 999px; padding: 6px 0; gap: 2px; background: repeating-linear-gradient(180deg, #3A3E44 0 2px, #24272B 2px 5px); box-shadow: 0 2px 0 #000;">
              <button type="button" aria-label="Rotella su" onPointerDown="{{kUp.down}}" onPointerUp="{{kUp.up}}" onPointerLeave="{{kUp.leave}}" onKeyDown="{{kUp.kd}}" onKeyUp="{{kUp.ku}}" onClick="{{kUp.click}}" onContextMenu="{{noCtx}}" style="background: transparent; border: 0; color: #F2F3F1; font: 600 13px/1 'IBM Plex Mono', monospace; padding: 10px 0; cursor: pointer; touch-action: none; user-select: none; transform: {{kUp.tf}};">▲</button>
              <button type="button" aria-label="Premi la rotella" onPointerDown="{{kOk.down}}" onPointerUp="{{kOk.up}}" onPointerLeave="{{kOk.leave}}" onKeyDown="{{kOk.kd}}" onKeyUp="{{kOk.ku}}" onClick="{{kOk.click}}" onContextMenu="{{noCtx}}" style="background: transparent; border: 0; color: #F2F3F1; font: 600 16px/1 'IBM Plex Mono', monospace; padding: 10px 0; cursor: pointer; touch-action: none; user-select: none; transform: {{kOk.tf}};">●</button>
              <button type="button" aria-label="Rotella giù" onPointerDown="{{kDown.down}}" onPointerUp="{{kDown.up}}" onPointerLeave="{{kDown.leave}}" onKeyDown="{{kDown.kd}}" onKeyUp="{{kDown.ku}}" onClick="{{kDown.click}}" onContextMenu="{{noCtx}}" style="background: transparent; border: 0; color: #F2F3F1; font: 600 13px/1 'IBM Plex Mono', monospace; padding: 10px 0; cursor: pointer; touch-action: none; user-select: none; transform: {{kDown.tf}};">▼</button>
            </div>
            <span style="font: 600 10px 'IBM Plex Mono', monospace; letter-spacing: 0.06em; color: #4A4F55; margin-top: -6px;">ROTELLA</span>
            <button type="button" aria-label="Tasto EXIT" onPointerDown="{{kExit.down}}" onPointerUp="{{kExit.up}}" onPointerLeave="{{kExit.leave}}" onKeyDown="{{kExit.kd}}" onKeyUp="{{kExit.ku}}" onClick="{{kExit.click}}" onContextMenu="{{noCtx}}" style="width: 100%; padding: 11px 0; border: 0; border-radius: 7px; cursor: pointer; touch-action: none; user-select: none; background: #2A2D31; color: #F2F3F1; font: 700 11px/1 'Chakra Petch', sans-serif; letter-spacing: 0.06em; box-shadow: {{kExit.sh}}; transform: {{kExit.tf}};">EXIT</button>
          </div>
        </div>
      </sc-if>

      <sc-if value="{{isReterm}}" hint-placeholder-val="{{false}}">
        <div style="display: flex; flex-direction: column; align-items: stretch;">
          <div style="display: flex; justify-content: flex-end; gap: 14px; padding: 0 48px;">
            <button type="button" aria-label="Tasto sinistro" onPointerDown="{{kL.down}}" onPointerUp="{{kL.up}}" onPointerLeave="{{kL.leave}}" onKeyDown="{{kL.kd}}" onKeyUp="{{kL.ku}}" onClick="{{kL.click}}" onContextMenu="{{noCtx}}" style="width: 70px; height: 24px; border: 1px solid #AEB3B8; border-bottom: 0; border-radius: 8px 8px 0 0; cursor: pointer; touch-action: none; user-select: none; background: #DDE0E3; color: #2A2D31; font: 700 12px/1 'IBM Plex Mono', monospace; transform: {{kL.tf}};">◀</button>
            <button type="button" aria-label="Tasto centrale" onPointerDown="{{kM.down}}" onPointerUp="{{kM.up}}" onPointerLeave="{{kM.leave}}" onKeyDown="{{kM.kd}}" onKeyUp="{{kM.ku}}" onClick="{{kM.click}}" onContextMenu="{{noCtx}}" style="width: 70px; height: 24px; border: 1px solid #AEB3B8; border-bottom: 0; border-radius: 8px 8px 0 0; cursor: pointer; touch-action: none; user-select: none; background: #DDE0E3; color: #2A2D31; font: 700 12px/1 'IBM Plex Mono', monospace; transform: {{kM.tf}};">●</button>
            <button type="button" aria-label="Tasto destro, verde" onPointerDown="{{kR.down}}" onPointerUp="{{kR.up}}" onPointerLeave="{{kR.leave}}" onKeyDown="{{kR.kd}}" onKeyUp="{{kR.ku}}" onClick="{{kR.click}}" onContextMenu="{{noCtx}}" style="width: 70px; height: 24px; border: 1px solid #6FA27A; border-bottom: 0; border-radius: 8px 8px 0 0; cursor: pointer; touch-action: none; user-select: none; background: #9ED9A8; color: #1C3A22; font: 700 12px/1 'IBM Plex Mono', monospace; transform: {{kR.tf}};">▶</button>
          </div>
          <div style="display: flex; flex-direction: column; gap: 10px; background: linear-gradient(160deg, #F3F4F4, #DADDE0); border: 1px solid #C2C6CA; border-radius: 18px; padding: 14px 20px 20px; box-shadow: 0 22px 44px -26px rgba(0, 0, 0, 0.6);">
            <div style="display: flex; align-items: center; gap: 8px; font: 600 10px 'IBM Plex Mono', monospace; letter-spacing: 0.06em; color: #6B737C;">
              <span style="width: 10px; height: 10px; border-radius: 50%; background: {{ledColor}}; box-shadow: {{ledGlow}};"></span>
              <span>LED</span>
              <span style="margin-left: auto; font: 700 12px 'IBM Plex Mono', monospace; color: #2646C4;">{{beepText}}</span>
              <span>CICALINO</span>
            </div>
            <div style="background: #1B1C1F; padding: 8px; border-radius: 6px;">
              <canvas ref="{{setCanvas}}" data-cal="{{uid}}" width="{{devW}}" height="{{devH}}" onClick="{{tap}}" role="img" aria-label="Schermo e-ink simulato" style="display: block; width: {{cssW}}px; height: {{cssH}}px; image-rendering: {{rendering}}; background: #D6D7CF; cursor: {{cursor}};"></canvas>
            </div>
          </div>
        </div>
      </sc-if>
    </div>

    <p style="margin: 0; font-size: 13px; line-height: 1.45; color: #56606B;">{{note}} Posizione dei tasti indicativa; velocità del pannello stimata.</p>
  </div>

  <div style="width: 320px; flex: none; display: flex; flex-direction: column; gap: 12px;">
    <div style="background: #F5F6F8; border: 1px solid #C6CCD3; border-radius: 8px; padding: 14px 16px; display: flex; flex-direction: column; gap: 8px;">
      <p style="margin: 0; font: 500 11px/1.4 'IBM Plex Mono', monospace; letter-spacing: 0.09em; text-transform: uppercase; color: #56606B;">Comandi di questo dispositivo</p>
      <sc-for list="{{controls}}" as="r" hint-placeholder-count="5">
        <div style="display: grid; grid-template-columns: 108px minmax(0, 1fr); gap: 10px; font-size: 13px; line-height: 1.4;">
          <span style="font: 600 12px/1.4 'IBM Plex Mono', monospace; color: #14181C;">{{r.k}}</span>
          <span style="color: #3D4650;">{{r.t}}</span>
        </div>
      </sc-for>
    </div>

    <div style="background: #F5F6F8; border: 1px solid #C6CCD3; border-radius: 8px; padding: 14px 16px; display: flex; flex-direction: column; gap: 6px;">
      <p style="margin: 0; font: 500 11px/1.4 'IBM Plex Mono', monospace; letter-spacing: 0.09em; text-transform: uppercase; color: #56606B;">Ultimo comando</p>
      <p style="margin: 0; font: 600 20px/1.2 'Chakra Petch', sans-serif;">{{lastInput}}</p>
      <p style="margin: 0; font-size: 14px; line-height: 1.45; color: #3D4650;">{{lastAction}}</p>
    </div>

    <div style="background: #F5F6F8; border: 1px solid #C6CCD3; border-radius: 8px; padding: 14px 16px; display: flex; flex-direction: column; gap: 8px;">
      <p style="margin: 0; font: 500 11px/1.4 'IBM Plex Mono', monospace; letter-spacing: 0.09em; text-transform: uppercase; color: #56606B;">Simula</p>
      <div style="display: flex; flex-wrap: wrap; gap: 6px;">
        <sc-for list="{{sims}}" as="s" hint-placeholder-count="9">
          <button type="button" aria-pressed="{{s.pressed}}" onClick="{{s.click}}" style="padding: 5px 9px; border-radius: 6px; border: 1px solid #2646C4; background: {{s.bg}}; color: {{s.fg}}; font: 600 12px/1.3 'IBM Plex Sans', sans-serif; cursor: pointer;">{{s.label}}</button>
        </sc-for>
      </div>
    </div>

    <div style="background: #F5F6F8; border: 1px solid #C6CCD3; border-radius: 8px; padding: 14px 16px; display: flex; flex-direction: column; gap: 6px;">
      <p style="margin: 0; font: 500 11px/1.4 'IBM Plex Mono', monospace; letter-spacing: 0.09em; text-transform: uppercase; color: #56606B;">Pannello</p>
      <p style="margin: 0; font: 400 12px/1.5 'IBM Plex Mono', monospace; color: #3D4650;">{{profile}}</p>
      <p style="margin: 0; font: 400 12px/1.5 'IBM Plex Mono', monospace; color: #3D4650;">Parziali dall’ultimo completo: {{partials}}/{{fullEvery}}</p>
      <p style="margin: 0; font: 700 12px/1.5 'IBM Plex Mono', monospace; color: #2646C4;">{{beepLine}}</p>
    </div>
  </div>
</div>`;
class Component extends DCLogic {
  static build() {
    const INK = '#000', PAPER = '#fff';
    const PAPER_RGB = [214, 215, 207], INK_RGB = [30, 32, 34];
    const F_UI = '"Chakra Petch", "Arial Narrow", sans-serif';
    const F_PIX = '"Silkscreen", monospace';
    const now = () => Date.now();
    const pad2 = (n) => String(n).padStart(2, '0');
    const hm = (d) => pad2(d.getHours()) + ':' + pad2(d.getMinutes());
    // 0.10.1: senza l'ora vera (né welcome né SNTP da questa accensione a freddo) l'orologio è «--:--», mai un'ora sbagliata.
    const clock = (m) => (m && m.sys.wallOk === false ? '--:--' : hm(new Date()));
    const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
    const fmt1 = (v) => v.toFixed(1).replace('.', ',');
    const yes = (v) => (v ? 'sì' : 'no');
    function dateStr(m) {
      if (m && m.sys.wallOk === false) return '';  // 0.10.1: senza l'ora vera niente data
      try { return new Date().toLocaleDateString('it-IT', { weekday: 'long', day: 'numeric', month: 'long' }); } catch (e) { return ''; }
    }
    function ago(t) {
      const m = Math.max(0, Math.round((now() - t) / 60000));
      return m < 60 ? m + ' min' : Math.floor(m / 60) + ' h ' + pad2(m % 60);
    }

    const DEVICES = {
      heltec213: { id: 'heltec213', name: 'Heltec Wireless Paper', size: '2,13″', W: 250, H: 122, cls: 'S', gray: false, fps: 2, fpsMax: 1.5, fullEvery: 40, input: 'single', shell: 'acrylic', scale: 3, keyName: 'PRG',
        spec: 'ESP32‑S3 · 250×122 · bianco/nero · 1 tasto (PRG)', note: 'Il tasto PRG è l’unico a disposizione: tutto passa dai gesti.' },
      t5213: { id: 't5213', name: 'LilyGO T5 V2.4', size: '2,13″', W: 250, H: 122, cls: 'S', gray: false, fps: 2, fullEvery: 40, input: 'single', shell: 'pcb', scale: 3, keyName: 'IO39',
        spec: 'ESP32 · 250×122 · bianco/nero · 1 tasto utente (IO39)', note: 'Stessa risoluzione della Heltec, quindi stessa interfaccia.' },
      crow42: { id: 'crow42', name: 'CrowPanel ESP32‑S3', size: '4,2″', W: 400, H: 300, cls: 'M', gray: false, fps: 2, fullEvery: 60, input: 'wheel', shell: 'crow', scale: 1.75, sd: true,
        spec: 'ESP32‑S3 · 400×300 · bianco/nero · rotella, MENU, EXIT · SD', note: 'Rotella su IO6, IO4 e IO5; MENU su IO2; EXIT su IO1.' },
      crow579: { id: 'crow579', name: 'CrowPanel ESP32‑S3', size: '5,79″', W: 792, H: 272, cls: 'W', gray: false, fps: 2, fullEvery: 60, input: 'wheel', shell: 'crow', scale: 0.9,
        spec: 'ESP32‑S3 · 792×272 · bianco/nero · rotella, MENU, EXIT', note: 'Due controller SSD1683, uno per metà schermo; stessi tasti del 4,2″.' },
      t547: { id: 't547', name: 'LilyGO T5 E‑Paper S3', size: '4,7″', W: 960, H: 540, cls: 'L', gray: true, fps: 4, fullEvery: 60, input: 'single', shell: 'pcb', scale: 0.78, keyName: 'IO48', touch: true,
        spec: 'ESP32‑S3 · 960×540 · 16 grigi · 1 tasto (IO48) · tocco opzionale', note: 'Il tocco c’è solo sulla versione touch: attivalo da Simula.' },
      reterm75: { id: 'reterm75', name: 'Seeed reTerminal E1001', size: '7,5″', W: 800, H: 480, cls: 'L', gray: false, fps: 1, fullEvery: 30, input: 'three', shell: 'reterm', scale: 0.95, sd: true, buzzer: true, led: true, sensor: true,
        spec: 'ESP32‑S3 · 800×480 · bianco/nero · 3 tasti · cicalino, LED, sensore T/UR', note: 'Da sinistra KEY2, KEY1 e KEY0, quello verde.' }
    };
    const STATES = {
      working: { label: 'LAVORA', long: 'Sta lavorando', prio: 3 },
      permission: { label: 'PERMESSO', long: 'Chiede un permesso', prio: 6 },
      input: { label: 'ATTENDE TE', long: 'Aspetta una risposta', prio: 5 },
      error: { label: 'ERRORE', long: 'Errore API', prio: 4 },
      compact: { label: 'RIORDINA', long: 'Riordina la memoria', prio: 3 },
      done: { label: 'FINITO', long: 'Turno completato', prio: 2 },
      idle: { label: 'IN PAUSA', long: 'Sessione aperta', prio: 1 }
    };
    const CONTROLS = {
      single: [
        ['1 pressione', 'Avanti: sessione, voce o pagina successiva'],
        ['2 pressioni', 'Apri o conferma'],
        ['3 pressioni', 'Menu; dal Menu torna alla Home'],
        ['Lunga 0,7 s', 'Indietro; nella Home silenzia per 1 ora'],
        ['Tieni 3 s', 'Riposo (nelle conferme: Proteggi, Ripristino). Da 0,7 s una barra si riempie; a 3 s parte subito, poi rilascia'],
        ['Nel permesso', '1 cambia scelta, 2 conferma, lunga annulla']
      ],
      wheel: [
        ['Rotella ▲ ▼', 'Scegli sessione o voce'],
        ['Rotella', 'Apri o conferma'],
        ['Rotella tenuta', 'Nella Home silenzia per 1 ora'],
        ['MENU', 'Menu; dal Menu torna alla Home'],
        ['MENU tenuto', 'Riposo'],
        ['EXIT', 'Indietro; nel permesso annulla'],
        ['EXIT tenuto', 'Torna alla Home']
      ],
      three: [
        ['◀ sinistro', 'Precedente · tenuto: indietro'],
        ['● centrale', 'Apri o conferma · tenuto: Menu'],
        ['▶ destro', 'Successivo · tenuto: silenzia in Home, altrove Home'],
        ['Nel permesso', '◀ ▶ scegli, ● conferma, ◀ tenuto annulla']
      ]
    };
    const HOME_LABEL = { goccia: 'Goccia', righe: 'Righe', contatori: 'Contatori', una: 'Una alla volta' };
    const ANIM_LABEL = { auto: 'Auto', raffiche: 'A raffiche', continue: 'Continue', spente: 'Spente' };
    const SPEED_LABEL = { profilo: 'Profilo', massima: 'Massima' };
    // Più reti (firmware 0.8.0): fino a SAVED_MAX reti salvate; il display usa la più forte tra quelle che vede.
    const SAVED_MAX = 5;
    const NETS = [
      { ssid: 'CasaWiFi', rssi: -58, lock: true },
      { ssid: 'Officina', rssi: -66, lock: true },
      { ssid: 'Ospiti', rssi: -71, lock: false },
      { ssid: 'TIM-29384712', rssi: -79, lock: true },
      { ssid: 'Vodafone-A1B2', rssi: -86, lock: true }
    ];
    const G = {
      plug: ['.#...#.', '.#...#.', '#######', '#######', '.#####.', '..###..', '...#...'],
      bell: ['...#...', '..###..', '.#####.', '.#####.', '.#####.', '#######', '...#...'],
      moon: ['..###..', '.##....', '##.....', '##.....', '##.....', '.##....', '..###..'],
      lock: ['..###..', '.#...#.', '.#...#.', '#######', '###.###', '###.###', '#######'],
      bolt: ['...##', '..##.', '.##..', '#####', '..##.', '.##..', '##...'],
      dot: ['..###..', '.#####.', '#######', '#######', '#######', '.#####.', '..###..'],
      glass: ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######'],
      gauge: ['.#####.', '#.....#', '#.#...#', '#..#..#', '#...#.#', '#.....#', '.#####.']
    };
    const ICONS = {
      done: ['.......', '......#', '.....##', '#...##.', '##.##..', '.###...', '..#....'],
      error: ['#.....#', '##...##', '.##.##.', '..###..', '.##.##.', '##...##', '#.....#'],
      input: ['.#####.', '##...##', '.....##', '...###.', '..##...', '.......', '..##...'],
      permission: ['..##...', '..##...', '..##...', '..##...', '..##...', '.......', '..##...'],
      idle: ['#######', '....##.', '...##..', '..##...', '.##....', '#######', '.......'],
      compact: ['#######', '.#####.', '.......', '..###..', '.......', '...#...', '...#...'],
      working0: ['#######', '.#####.', '..###..', '...#...', '..#.#..', '.#...#.', '#######'],
      working1: ['#######', '.#...#.', '..#.#..', '...#...', '..###..', '.#####.', '#######']
    };
    const HW = [1, 1, 2, 2, 3, 4, 5, 6, 7, 7, 8, 8, 8, 8, 8, 7, 7, 6, 4];

    function MET(dev) {
      if (dev.cls === 'S') return { sb: 0, f1: 20, f2: 12, f3: 10, row: 18, pad: 6, hint: 0 };
      if (dev.cls === 'M') return { sb: 24, f1: 17, f2: 14, f3: 11, row: 28, pad: 8, hint: 20 };
      if (dev.cls === 'W') return { sb: 24, f1: 16, f2: 14, f3: 11, row: 26, pad: 10, hint: 20 };
      const k = dev.H / 480;
      return { sb: Math.round(32 * k), f1: Math.round(26 * k), f2: Math.round(18 * k), f3: Math.round(14 * k), row: Math.round(38 * k), pad: Math.round(14 * k), hint: Math.round(26 * k) };
    }

    /* ---------- primitive ---------- */
    function fit(c, s, max, ell) {
      s = String(s);
      if (c.measureText(s).width <= max) return s;
      while (s.length > 1 && c.measureText(s + ell).width > max) s = s.slice(0, -1);
      return s.trimEnd() + ell;
    }
    function T(c, s, x, y, size, o) {
      o = o || {};
      const fam = o.f || F_UI;
      c.font = (o.w || 600) + ' ' + size + 'px ' + fam;
      c.textAlign = o.a || 'left';
      c.textBaseline = 'top';
      c.fillStyle = o.c || INK;
      let str = String(s);
      if (o.max) str = fit(c, str, o.max, fam === F_PIX ? '..' : '…');
      c.fillText(str, Math.round(x), Math.round(y));
    }
    function wrapLines(c, str, size, maxW, w, fam) {
      c.font = (w || 500) + ' ' + size + 'px ' + (fam || F_UI);
      const words = String(str).split(/\s+/), lines = [];
      let line = '';
      for (const wd of words) {
        const t = line ? line + ' ' + wd : wd;
        if (c.measureText(t).width <= maxW) line = t;
        else { if (line) lines.push(line); line = wd; }
      }
      if (line) lines.push(line);
      return lines;
    }
    function wrapT(c, str, x, y, size, maxW, maxLines, lh, o) {
      o = o || {};
      wrapLines(c, str, size, maxW, o.w || 500, o.f).slice(0, maxLines).forEach((l, i) => T(c, l, x, y + i * lh, size, Object.assign({}, o, { max: maxW })));
    }
    function box(c, x, y, w, h, col, t) {
      t = t || 1;
      c.fillStyle = col || INK;
      c.fillRect(x, y, w, t); c.fillRect(x, y + h - t, w, t); c.fillRect(x, y, t, h); c.fillRect(x + w - t, y, t, h);
    }
    function dotsH(c, x0, x1, y) { c.fillStyle = INK; for (let x = x0; x < x1; x += 3) c.fillRect(x, y, 1, 1); }
    function dotsV(c, x, y0, y1) { c.fillStyle = INK; for (let y = y0; y < y1; y += 2) c.fillRect(x, y, 1, 1); }
    function glyph(c, rows, x, y, s, col) {
      c.fillStyle = col;
      for (let r = 0; r < rows.length; r++) for (let q = 0; q < rows[r].length; q++) if (rows[r][q] === '#') c.fillRect(x + q * s, y + r * s, s, s);
    }
    function strike(c, x, y, n, s, col, bg) {
      for (let k = 0; k < n; k++) {
        c.fillStyle = bg; c.fillRect(x + (k + 1) * s, y + k * s, s, s);
        c.fillStyle = col; c.fillRect(x + k * s, y + k * s, s, s);
      }
    }
    function tri(c, x, y, s, dir) {
      c.fillStyle = INK;
      for (let k = 0; k < 4; k++) {
        if (dir === 'up') c.fillRect(x + (3 - k) * s, y + k * s, (1 + 2 * k) * s, s);
        else if (dir === 'down') c.fillRect(x + k * s, y + k * s, (7 - 2 * k) * s, s);
        else if (dir === 'left') c.fillRect(x + k * s, y + (3 - k) * s, s, (1 + 2 * k) * s);
        else c.fillRect(x + k * s, y + k * s, s, (7 - 2 * k) * s);
      }
    }
    function chevL(c, x, y, col, s) {
      s = s || 1;
      c.fillStyle = col;
      for (let r = 0; r < 5; r++) c.fillRect(x + (4 - r) * s, y + r * s, 2 * s, s);
      for (let r = 0; r < 4; r++) c.fillRect(x + (1 + r) * s, y + (5 + r) * s, 2 * s, s);
    }
    function icon(c, st, x, y, s, f, col) {
      const b = ICONS[st === 'working' ? 'working' + (((f % 2) + 2) % 2) : st];
      glyph(c, b, x, y, s, col || INK);
    }
    function bar(c, x, y, w, h, s, f, gray) {
      box(c, x, y, w, h, INK, 1);
      const ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;
      if (ih <= 0 || iw <= 0) return;
      if (gray) { c.fillStyle = '#d4d4d4'; c.fillRect(ix, iy, iw, ih); }
      if (s.total > 0) {
        const per = iw / s.total, fw = Math.round(per * s.done);
        c.fillStyle = INK; c.fillRect(ix, iy, fw, ih);
        if (s.state === 'working' && s.done < s.total) {
          const sx = ix + fw, sw = Math.max(2, Math.round(per));
          if (gray) {
            c.fillStyle = '#8a8a8a'; c.fillRect(sx, iy, sw, ih);
            c.fillStyle = '#444444'; const p = (f * 4) % sw; c.fillRect(sx + p, iy, Math.min(4, sw - p), ih);
          } else {
            c.fillStyle = INK;
            for (let py = 0; py < ih; py++) for (let px = 0; px < sw; px++) if (((px + py + f) % 4) === 0) c.fillRect(sx + px, iy + py, 1, 1);
          }
        }
        if (s.total <= 16) {
          for (let k = 1; k < s.total; k++) {
            const tx = ix + Math.round(per * k);
            if (tx < ix + fw) { c.fillStyle = gray ? '#d4d4d4' : PAPER; c.fillRect(tx, iy, 1, ih); }
            else { c.fillStyle = INK; c.fillRect(tx, iy + ih - 2, 1, 2); }
          }
        }
      } else {
        c.fillStyle = gray ? '#777777' : INK;
        const mv = s.state === 'working' ? f * 2 : 0;
        for (let px = 0; px < iw; px++) if (((px + mv) % 8) < 3) c.fillRect(ix + px, iy, 1, ih);
      }
    }
    function checkbox(c, x, y, st, f, sz) {
      sz = sz || 10;
      if (st === 2) {
        c.fillStyle = INK; c.fillRect(x, y, sz, sz);
        c.fillStyle = PAPER;
        const k = sz / 10;
        [[2, 5], [3, 6], [4, 7], [5, 6], [6, 5], [7, 4], [8, 3]].forEach((p) => c.fillRect(Math.round(x + p[0] * k), Math.round(y + (p[1] - 1) * k), Math.max(1, Math.round(k)), Math.max(2, Math.round(2 * k))));
      } else {
        box(c, x, y, sz, sz, INK, 1);
        if (st === 1) { c.fillStyle = INK; for (let yy = 2; yy < sz - 2; yy++) for (let xx = 2; xx < sz - 2; xx++) if ((xx + yy + f) % 2 === 0) c.fillRect(x + xx, y + yy, 1, 1); }
      }
    }
    function wifiIcon(c, x, y, s, level, col) {
      c.fillStyle = col;
      for (let i = 0; i < 4; i++) {
        const h = (3 + 2 * i) * s, bx = x + i * 3 * s;
        if (level > i) c.fillRect(bx, y + 9 * s - h, 2 * s, h); else c.fillRect(bx, y + 8 * s, 2 * s, s);
      }
      if (level < 0) for (let k = 0; k < 5; k++) { c.fillRect(x + k * s, y + k * s, s, s); c.fillRect(x + (4 - k) * s, y + k * s, s, s); }
    }
    function battIcon(c, x, y, s, pct, col) {
      box(c, x, y, 14 * s, 8 * s, col, s);
      c.fillStyle = col;
      c.fillRect(x + 14 * s, y + 2 * s, 2 * s, 4 * s);
      c.fillRect(x + 2 * s, y + 2 * s, Math.round(10 * s * clamp(pct, 0, 100) / 100), 4 * s);
    }
    function mascot(c, ox, oy, s, st, f, ink, paper) {
      ink = ink || INK; paper = paper || PAPER;
      const fr = ((f % 4) + 4) % 4;
      const P = (x, y, col) => { c.fillStyle = col; c.fillRect(ox + x * s, oy + y * s, s, s); };
      const I = (x, y) => P(x, y, ink), O = (x, y) => P(x, y, paper);
      let dy = 0, sq = 0;
      if (st === 'done') dy = [0, -2, -3, -1][fr];
      else if (st === 'working') dy = [0, 0, 1, 0][fr];
      else if (st === 'permission') dy = [0, -1, 0, -1][fr];
      else if (st === 'error') dy = 1;
      else if (st === 'idle') sq = fr > 1 ? 1 : 0;
      const top = 4 + dy, cx = 13;
      for (let r = sq; r < HW.length; r++) { const h = HW[r]; for (let x = cx - h; x < cx + h; x++) I(x, top + r); }
      if (st === 'done' && dy <= -2) { I(10, top + 19); I(15, top + 19); } else { I(9, top + 19); I(10, top + 19); I(15, top + 19); I(16, top + 19); }
      const ey = top + 9;
      const eye = (ex, k) => {
        if (k === 'closed') { for (let x = 0; x < 4; x++) O(ex + x, ey + 2); return; }
        if (k === 'happy') { O(ex, ey + 2); O(ex + 1, ey + 1); O(ex + 2, ey + 1); O(ex + 3, ey + 2); return; }
        if (k === 'x') { O(ex, ey); O(ex + 3, ey); O(ex + 1, ey + 1); O(ex + 2, ey + 1); O(ex + 1, ey + 2); O(ex + 2, ey + 2); O(ex, ey + 3); O(ex + 3, ey + 3); return; }
        for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) O(ex + x, ey + y);
        let px = ex + 1, py = ey + 1;
        if (k === 'down') py = ey + 2; else if (k === 'up') { py = ey; px = ex + 2; }
        I(px, py); I(px + 1, py); I(px, py + 1); I(px + 1, py + 1);
      };
      const eyes = (k) => { eye(7, k); eye(15, k); };
      const mouth = (k) => {
        const y = top + 14;
        if (k === 'grin') { O(11, y); O(12, y); O(13, y); O(14, y); O(12, y + 1); O(13, y + 1); }
        else if (k === 'o') { O(12, y); O(13, y); O(12, y + 1); O(13, y + 1); }
        else if (k === 'frown') { O(11, y + 1); O(12, y); O(13, y); O(14, y + 1); }
        else { O(11, y + 1); O(12, y + 1); O(13, y + 1); O(14, y + 1); }
      };
      const arm = (k, side) => {
        const mm = side < 0 ? ((x) => 25 - x) : ((x) => x), A = (x, y) => I(mm(x), top + y);
        if (k === 'up0') { A(21, 12); A(22, 11); A(23, 10); A(23, 9); }
        else if (k === 'up1') { A(21, 12); A(22, 12); A(23, 11); A(24, 10); }
        else if (k === 'rest') { A(21, 13); A(22, 14); }
        else if (k === 'type') { A(21, 13); A(22, 13); }
        else if (k === 'droop') { A(21, 14); A(22, 15); }
        else if (k === 'cheer') { A(21, 11); A(22, 10); A(23, 9); }
      };
      const bubble = (inv) => {
        for (let x = 19; x <= 25; x++) { I(x, 0); I(x, 6); }
        for (let y = 1; y <= 5; y++) { I(18, y); I(26, y); }
        for (let y = 1; y <= 5; y++) for (let x = 19; x <= 25; x++) P(x, y, inv ? ink : paper);
        I(18, 7); I(17, 8);
      };
      const Gp = (inv) => (x, y) => P(x, y, inv ? paper : ink);
      const plus = (x, y) => { I(x, y - 1); I(x - 1, y); I(x, y); I(x + 1, y); I(x, y + 1); };
      const zz = (x, y) => { I(x, y); I(x + 1, y); I(x + 2, y); I(x + 1, y + 1); I(x, y + 2); I(x + 1, y + 2); I(x + 2, y + 2); };
      if (st === 'working') { eyes(fr === 3 ? 'open' : 'down'); mouth('flat'); arm(fr % 2 ? 'type' : 'rest', 1); arm(fr % 2 ? 'rest' : 'type', -1); bubble(false); const g = Gp(false); [20, 22, 24].forEach((x, k) => g(x, k === fr % 3 ? 2 : 3)); }
      else if (st === 'input') { eyes(fr === 3 ? 'closed' : 'up'); mouth('o'); arm(fr % 2 ? 'up1' : 'up0', 1); arm('rest', -1); bubble(false); const g = Gp(false); g(21, 1); g(22, 1); g(23, 1); g(23, 2); g(22, 3); g(22, 5); }
      else if (st === 'permission') { const inv = fr % 2 === 1; eyes('open'); mouth('o'); arm(fr % 2 ? 'up1' : 'up0', 1); arm(fr % 2 ? 'up0' : 'up1', -1); bubble(inv); const g = Gp(inv); g(22, 1); g(22, 2); g(22, 3); g(22, 5); }
      else if (st === 'done') { eyes('happy'); mouth('grin'); if (dy < 0) { arm('cheer', 1); arm('cheer', -1); } else { arm('rest', 1); arm('rest', -1); } if (fr % 2 === 0) { plus(3, 3); plus(23, 5); } else { plus(2, 9); plus(24, 1); } }
      else if (st === 'error') { eyes('x'); mouth('frown'); arm('droop', 1); arm('droop', -1); const d = fr % 2; I(19, top + 3 + d); I(19, top + 4 + d); I(20, top + 4 + d); }
      else if (st === 'compact') { eyes('up'); mouth('flat'); arm(fr % 2 ? 'type' : 'rest', 1); arm(fr % 2 ? 'rest' : 'type', -1); bubble(false); const g = Gp(false); [2, 4].forEach((yy, k) => { const len = (fr + k) % 2 ? 3 : 5; for (let x = 0; x < len; x++) g(20 + x, yy); }); }
      else if (st === 'tired') { eyes('closed'); O(7, ey + 1); O(10, ey + 1); O(15, ey + 1); O(18, ey + 1); mouth('flat'); arm('droop', 1); arm('droop', -1); const d = fr % 2; I(5, top + 4 + d); I(5, top + 5 + d); I(4, top + 5 + d); }
      else if (st === 'stuffy') { eyes('closed'); mouth('o'); arm('droop', 1); arm('droop', -1); const w = fr % 2; [[1, 1], [4, 0], [21, 1], [24, 0]].forEach((p) => { I(p[0], p[1] + w); I(p[0] + 1, p[1] + 1 - w); I(p[0] + 2, p[1] + w); }); }
      else { eyes('closed'); mouth('flat'); const Z = [[18, 8], [20, 5], [22, 2], null]; [Z[fr], Z[(fr + 2) % 4]].forEach((p) => { if (p) zz(p[0], p[1]); }); }
    }
    /* ---------- avatar (docs/AVATAR.md) ----------
     * Libreria in assets/avatars.js (window.CALAMAIO_AVATARS: CAVT v1 in base64, generato da avatars/*.avatar.json
     * con tools/sim-capture/gen-avatars.mjs). Si valida come fa il firmware (parseAvatar in firmware/src/gfx/avatar.cpp):
     * un CAVT rovinato non si carica e al suo posto si vede la Goccia. La Goccia resta mascot(): il suo CAVT è
     * generato da mascot() e il firmware lo verifica pixel per pixel (test_mascot).
     * Precedenza come sul display: s.avatar → se manca la Goccia; sessione senza avatar → set.defaultAvatar → Goccia. */
    const AV_W = 28, AV_H = 25, AV_FB = 175, AV_POSES = ['working', 'permission', 'input', 'done', 'error', 'idle', 'compact', 'tired', 'stuffy', 'run'];
    const AV_FALLBACK = { compact: 'working', tired: 'idle', stuffy: 'idle', run: 'working' };  // run: AVATAR.md §12
    function avCrc32(b, from, to) {
      let c = 0xFFFFFFFF;
      for (let i = from; i < to; i++) { c ^= b[i]; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; }
      return (c ^ 0xFFFFFFFF) >>> 0;
    }
    function avB64(s) {
      const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/', out = [];
      let acc = 0, bits = 0;
      for (const ch of s.replace(/=+$/, '')) {
        const v = A.indexOf(ch); if (v < 0) return null;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push((acc >> bits) & 255); }
      }
      return Uint8Array.from(out);
    }
    // Stessi controlli e stesso ordine di parseAvatar() del firmware: null se il CAVT non è valido.
    function parseAvatar(b) {
      if (!b || b.length < 72 || String.fromCharCode(b[0], b[1], b[2], b[3]) !== 'CAVT' || b[4] !== 1 || b[5] !== 72) return null;
      const u16 = (o) => b[o] | (b[o + 1] << 8), total = u16(6), crc = (b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24)) >>> 0;
      if (total !== b.length || crc !== avCrc32(b, 12, b.length)) return null;
      const frames = b[14], mask = u16(16);
      if (b[12] !== AV_W || b[13] !== AV_H || (frames !== 2 && frames !== 4) || b[15] !== 0) return null;
      if ((mask & 0x3F) !== 0x3F || (mask >> 10) || u16(18) !== AV_FB) return null;  // bit 9 = run (AVATAR.md §12)
      let np = 0; for (let i = 0; i < 10; i++) if (mask & (1 << i)) np++;
      if (72 + np * frames * AV_FB !== b.length) return null;
      const str = (o) => { let e = o; while (e < o + 24 && b[e]) e++; for (let k = e; k < o + 24; k++) if (b[k]) return ''; return new TextDecoder().decode(b.subarray(o, e)); };
      const id = str(20), name = str(44);
      if (!/^[a-z0-9]+(-[a-z0-9]+)*$/.test(id) || id.length > 24 || !name) return null;
      for (let i = 72; i < b.length; i++) if (b[i] & 0xAA & (b[i] << 1)) return null;  // codice 11 riservato
      return { id: id, name: name, crc: crc, frames: frames, mask: mask, data: b };
    }
    const AVATARS = {}, AV_ORDER = [];
    ((typeof window !== 'undefined' && window.CALAMAIO_AVATARS) || []).forEach((e) => {
      const a = parseAvatar(avB64(e.cavt || ''));
      if (a && a.id === e.id) { AVATARS[a.id] = a; AV_ORDER.push(a.id); }
      else if (typeof console !== 'undefined') console.warn('Calamaio: avatar «' + e.id + '» non valido, si usa la Goccia');
    });
    // drawAvatar() del firmware: posa con ripiego, f mod frames, '#' inchiostro, '+' carta, '.' niente.
    function drawAvatar(c, a, ox, oy, s, st, f, ink, paper) {
      ink = ink || INK; paper = paper || PAPER;
      let pi = AV_POSES.indexOf(st);
      if (pi < 0 || !(a.mask & (1 << pi))) pi = AV_POSES.indexOf(AV_FALLBACK[st] || 'working');
      let idx = 0; for (let i = 0; i < pi; i++) if (a.mask & (1 << i)) idx++;
      const off = 72 + (idx * a.frames + (((f % a.frames) + a.frames) % a.frames)) * AV_FB;
      for (let k = 0; k < AV_W * AV_H; k++) {
        const v = (a.data[off + (k >> 2)] >> (6 - 2 * (k & 3))) & 3;
        if (!v) continue;
        c.fillStyle = v === 1 ? ink : paper;
        c.fillRect(ox + (k % AV_W) * s, oy + Math.floor(k / AV_W) * s, s, s);
      }
    }
    // L'avatar da disegnare (null = la Goccia, cioè mascot()).
    function avatarOf(m, s) {
      const id = s && s.avatar ? s.avatar : m.set.defaultAvatar;
      const a = AVATARS[id];
      return a && a.id !== 'goccia' ? a : null;
    }
    // Mascotte di una sessione (s) o, senza sessione (s = null), quella predefinita. Con il gioco acceso porta
    // l'accessorio indossato (accOverlay): si ridisegnano solo i punti che l'accessorio e il suo contorno cambiano.
    function figure(c, m, s, ox, oy, sc, st, f) {
      const a = avatarOf(m, s);
      if (a) drawAvatar(c, a, ox, oy, sc, st, f); else mascot(c, ox, oy, sc, st, f);
      accOverlay(c, m, a || AVATARS.goccia, ox, oy, sc, st, f);
    }
    function accOverlay(c, m, a, ox, oy, sc, st, f) {
      if (!a || !wornAcc(m)) return;
      const g0 = avGrid(a, st, f), g = figGrid(m, a, st, f);
      for (let k = 0; k < AV_W * AV_H; k++) {
        if (g[k] === g0[k]) continue;
        c.fillStyle = g[k] === 1 ? INK : PAPER;
        c.fillRect(ox + (k % AV_W) * sc, oy + Math.floor(k / AV_W) * sc, sc, sc);
      }
    }
    // Festa di un traguardo (gioco): coriandoli e scintille attorno alla mascotte, nei punti della griglia 28×25 rimasti
    // trasparenti (mascotte e accessorio compresi). Due serie che si alternano coi fotogrammi pari e dispari.
    const CONFETTI = [
      [[2, 1], [6, 4], [10, 0], [17, 2], [22, 0], [26, 4], [1, 8], [26, 10], [0, 15], [27, 17], [3, 21], [24, 22]],
      [[4, 0], [8, 2], [14, 1], [19, 4], [24, 1], [27, 6], [0, 11], [25, 13], [2, 18], [26, 20], [1, 23], [23, 24]]
    ];
    const SPARKS = [[[5, 9], [22, 7]], [[3, 5], [24, 16]]];
    function partyFx(c, m, s, ox, oy, sc, f) {
      const a = avatarOf(m, s) || AVATARS.goccia;
      if (!a) return;
      const g = figGrid(m, a, 'done', f), k = ((f % 2) + 2) % 2, pts = CONFETTI[k].slice();
      SPARKS[k].forEach((p) => { pts.push(p, [p[0] - 1, p[1]], [p[0] + 1, p[1]], [p[0], p[1] - 1], [p[0], p[1] + 1]); });
      c.fillStyle = INK;
      pts.forEach((p) => {
        if (p[0] < 0 || p[1] < 0 || p[0] >= AV_W || p[1] >= AV_H || g[p[1] * AV_W + p[0]]) return;
        c.fillRect(ox + p[0] * sc, oy + p[1] * sc, sc, sc);
      });
    }
    const avName = (id) => (AVATARS[id] ? AVATARS[id].name : id === 'goccia' ? 'Goccia' : id);
    /* ---------- accessori della mascotte (gioco, docs/AVATAR.md §13–14) ----------
     * assets/avatars.js porta anche window.CALAMAIO_ANCHORS (CANC v1 dei predefiniti, legati al CRC del loro CAVT) e
     * window.CALAMAIO_ACCESSORIES (CACC v1 del catalogo). Si validano come fa il firmware (parseAccessory e parseAnchors in
     * firmware/src/gfx/accessory.cpp): un blocco rovinato non si carica e quell'accessorio non si disegna. drawAccessory è
     * la copia di drawAccessory() di tools/avatar/lib/draw-accessory.mjs (riferimento), su una griglia 28×25 di codici
     * 0 trasparente, 1 inchiostro, 2 carta; i golden di tools/golden/accessory-golden.mjs la confrontano col riferimento. */
    const ANCHOR_NAMES = ['head-top', 'eyes', 'mouth', 'neck', 'hand'];
    const ACC_MAX_W = 14, ACC_MAX_H = 10, ACC_MAX_BYTES = 1024, ACC_HEADER = 72, ANC_HEADER = 20;
    const kebab = (s) => /^[a-z0-9]+(-[a-z0-9]+)*$/.test(s) && s.length <= 24;
    // Stessi controlli e stesso ordine di parseAccessory() del firmware (unpackAccessory di lib/accessory.mjs): null se non va.
    function parseAccessory(b) {
      if (!b || b.length < ACC_HEADER || String.fromCharCode(b[0], b[1], b[2], b[3]) !== 'CACC' || b[4] !== 1 || b[5] !== ACC_HEADER) return null;
      const u16 = (o) => b[o] | (b[o + 1] << 8), total = u16(6), crc = (b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24)) >>> 0;
      if (total !== b.length || total > ACC_MAX_BYTES || crc !== avCrc32(b, 12, total)) return null;
      const w = b[12], h = b[13], frames = b[14], anchor = b[15];
      if (w < 1 || w > ACC_MAX_W || h < 1 || h > ACC_MAX_H || (frames !== 1 && frames !== 2) || anchor >= ANCHOR_NAMES.length) return null;
      if (b[16] >= w || b[17] >= h || (b[18] >> 2) || (u16(20) >> 10)) return null;
      const fb = u16(22);
      if (fb !== Math.ceil(w * h / 4)) return null;
      const str = (o, kb) => { let e = o; while (e < o + 24 && b[e]) e++; for (let k = e; k < o + 24; k++) if (b[k]) return ''; const t = new TextDecoder().decode(b.subarray(o, e)); return kb ? (kebab(t) ? t : '') : (/[\u0000-\u001f\u007f]/.test(t) ? '' : t); };
      const id = str(24, true), name = str(48, false);
      if (!id || !name) return null;
      const retouch = {};
      let off = ACC_HEADER;
      for (let k = 0; k < b[19]; k++) {
        if (off + 1 > total) return null;
        const len = b[off++];
        if (len < 1 || len > 24 || off + len + 3 > total) return null;
        const rid = String.fromCharCode.apply(null, Array.from(b.subarray(off, off + len)));
        off += len;
        if (!kebab(rid) || retouch[rid]) return null;
        const dx = (b[off] << 24) >> 24, dy = (b[off + 1] << 24) >> 24, has = b[off + 2];
        off += 3;
        if (has > 1) return null;
        let px = -1;
        if (has) { if (off + frames * fb > total) return null; px = off; off += frames * fb; }
        retouch[rid] = { dx: dx, dy: dy, px: px };
      }
      if (off + frames * fb !== total) return null;
      return { id: id, name: name, crc: crc, w: w, h: h, frames: frames, anchor: anchor, ox: b[16], oy: b[17], outline: !!(b[18] & 1), special: !!(b[18] & 2),
        hide: u16(20), fb: fb, retouch: retouch, px: off, data: b };
    }
    // Blocco CANC v1 (AVATAR.md §13.4), come parseAnchors() del firmware: null se non va. avatarCrc lo lega al disegno.
    function parseAnchors(b) {
      if (!b || b.length < ANC_HEADER || String.fromCharCode(b[0], b[1], b[2], b[3]) !== 'CANC' || b[4] !== 1 || b[5] !== ANC_HEADER) return null;
      const u16 = (o) => b[o] | (b[o + 1] << 8), total = u16(6), crc = (b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24)) >>> 0;
      if (total !== b.length || crc !== avCrc32(b, 12, total)) return null;
      const avatarCrc = (b[12] | (b[13] << 8) | (b[14] << 16) | (b[15] << 24)) >>> 0, mask = u16(16), frames = b[18];
      if (b[19] !== ANCHOR_NAMES.length || (frames !== 2 && frames !== 4) || (mask >> 10)) return null;
      let np = 0; for (let i = 0; i < 10; i++) if (mask & (1 << i)) np++;
      if (total !== ANC_HEADER + np * frames * ANCHOR_NAMES.length * 2) return null;
      for (let o = ANC_HEADER; o < total; o += 2) {
        if (b[o] === 0xFF && b[o + 1] === 0xFF) continue;
        if (b[o] >= AV_W || b[o + 1] >= AV_H) return null;
      }
      return { avatarCrc: avatarCrc, mask: mask, frames: frames, data: b };
    }
    const ANCHORS = {}, ACCESSORIES = {}, ACC_ORDER = [];
    ((typeof window !== 'undefined' && window.CALAMAIO_ANCHORS) || []).forEach((e) => {
      const n = parseAnchors(avB64(e.canc || ''));
      if (n && AVATARS[e.avatar] && n.avatarCrc === AVATARS[e.avatar].crc) ANCHORS[e.avatar] = n;
      else if (typeof console !== 'undefined') console.warn('Calamaio: ancore di «' + e.avatar + '» non valide: niente accessori su quell\'avatar');
    });
    ((typeof window !== 'undefined' && window.CALAMAIO_ACCESSORIES) || []).forEach((e) => {
      const a = parseAccessory(avB64(e.cacc || ''));
      if (a && a.id === e.id) { ACCESSORIES[a.id] = a; ACC_ORDER.push(a.id); }
      else if (typeof console !== 'undefined') console.warn('Calamaio: accessorio «' + e.id + '» non valido');
    });
    // Punto di aggancio `which` (indice in ANCHOR_NAMES) della posa st e del fotogramma f: getAnchor() di lib/anchors.mjs
    // (ripiego della posa se l'avatar non la disegna, f mod frames delle ancore). null = assente: mai posizioni inventate.
    function accPoint(a, anc, st, f, which) {
      if (!anc) return null;
      let pi = AV_POSES.indexOf(st);
      if (pi < 0) return null;
      if (!(a.mask & (1 << pi)) && AV_FALLBACK[st] && (a.mask & (1 << AV_POSES.indexOf(AV_FALLBACK[st])))) pi = AV_POSES.indexOf(AV_FALLBACK[st]);
      if (!(anc.mask & (1 << pi))) return null;
      let idx = 0; for (let i = 0; i < pi; i++) if (anc.mask & (1 << i)) idx++;
      const o = ANC_HEADER + ((idx * anc.frames + (((f % anc.frames) + anc.frames) % anc.frames)) * ANCHOR_NAMES.length + which) * 2;
      return anc.data[o] === 0xFF && anc.data[o + 1] === 0xFF ? null : [anc.data[o], anc.data[o + 1]];
    }
    const NB8 = [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]];
    // Disegna l'accessorio sulla griglia g (Uint8Array 28×25, già con la mascotte di st e f). false = non si disegna
    // (posa chiesta nascosta, ancora assente). Ordine del riferimento: contorno, poi accessorio; 'x' = l'opposto di sotto.
    function drawAccessory(g, a, anc, acc, st, f) {
      const pi = AV_POSES.indexOf(st);
      if (!acc || pi < 0 || (acc.hide & (1 << pi))) return false;
      const pt = accPoint(a, anc, st, f, acc.anchor);
      if (!pt) return false;
      const r = Object.prototype.hasOwnProperty.call(acc.retouch, a.id) ? acc.retouch[a.id] : null;
      const base = (r && r.px >= 0 ? r.px : acc.px) + (((f % acc.frames) + acc.frames) % acc.frames) * acc.fb;
      const ox = pt[0] - acc.ox + (r ? r.dx : 0), oy = pt[1] - acc.oy + (r ? r.dy : 0), cells = [];
      for (let y = 0; y < acc.h; y++) for (let x = 0; x < acc.w; x++) {
        const i = y * acc.w + x, sym = (acc.data[base + (i >> 2)] >> (6 - 2 * (i & 3))) & 3;
        const gx = ox + x, gy = oy + y;
        if (!sym || gx < 0 || gy < 0 || gx >= AV_W || gy >= AV_H) continue;
        const k = gy * AV_W + gx;
        cells.push([k, sym === 3 ? (g[k] === 1 ? 2 : 1) : sym]);
      }
      if (acc.outline) {
        const occ = new Set(cells.map((c) => c[0])), ring = new Set();
        cells.forEach((c) => {
          if (c[1] !== 1) return;
          const cx = c[0] % AV_W, cy = (c[0] - cx) / AV_W;
          NB8.forEach((d) => {
            const x = cx + d[0], y = cy + d[1], k = y * AV_W + x;
            if (x >= 0 && y >= 0 && x < AV_W && y < AV_H && !occ.has(k) && g[k] === 1) ring.add(k);
          });
        });
        ring.forEach((k) => { g[k] = 2; });
      }
      cells.forEach((c) => { g[c[0]] = c[1]; });
      return true;
    }
    // Fotogramma della mascotte come griglia di codici (posa con ripiego, f mod frames), dai byte del CAVT.
    function avGrid(a, st, f) {
      let pi = AV_POSES.indexOf(st);
      if (pi < 0 || !(a.mask & (1 << pi))) pi = AV_POSES.indexOf(AV_FALLBACK[st] || 'working');
      let idx = 0; for (let i = 0; i < pi; i++) if (a.mask & (1 << i)) idx++;
      const off = 72 + (idx * a.frames + (((f % a.frames) + a.frames) % a.frames)) * AV_FB, g = new Uint8Array(AV_W * AV_H);
      for (let k = 0; k < AV_W * AV_H; k++) g[k] = (a.data[off + (k >> 2)] >> (6 - 2 * (k & 3))) & 3;
      return g;
    }
    // Accessorio indossato, solo con il gioco acceso (null = nessuno, o non ancora arrivato dal PC).
    function wornAcc(m) {
      const w = petOn(m) && m.pet.wear;
      const a = w && ACCESSORIES[w.id];
      return a && (w.crc == null || a.crc === w.crc) ? a : null;
    }
    // Mascotte di st e f con l'accessorio indossato sopra (a = avatar disegnato: la Goccia è il suo CAVT, identico a mascot()).
    function figGrid(m, a, st, f) {
      const g = avGrid(a, st, f), acc = wornAcc(m);
      if (acc) drawAccessory(g, a, ANCHORS[a.id] || null, acc, st, f);
      return g;
    }
    // N moduli per lato: 25 (Wi-Fi ospiti) o 29 (Configura Wi-Fi: versione 3, come il QR vero del firmware).
    function drawQR(c, x, y, px, seed, N) {
      N = N || 25;
      let h = 2166136261;
      for (let i = 0; i < seed.length; i++) { h ^= seed.charCodeAt(i); h = Math.imul(h, 16777619); }
      if (!h) h = 1;
      const rnd = () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 1000) / 1000; };
      c.fillStyle = PAPER; c.fillRect(x - px, y - px, (N + 2) * px, (N + 2) * px);
      const inFinder = (r, q) => (r < 8 && q < 8) || (r < 8 && q >= N - 8) || (r >= N - 8 && q < 8);
      for (let r = 0; r < N; r++) {
        for (let q = 0; q < N; q++) {
          if (inFinder(r, q)) continue;
          let on;
          if (r === 6) on = q % 2 === 0; else if (q === 6) on = r % 2 === 0; else on = rnd() < 0.48;
          if (on) { c.fillStyle = INK; c.fillRect(x + q * px, y + r * px, px, px); }
        }
      }
      const finder = (fx, fy) => {
        for (let r = 0; r < 7; r++) for (let q = 0; q < 7; q++) {
          const on = r === 0 || r === 6 || q === 0 || q === 6 || (r >= 2 && r <= 4 && q >= 2 && q <= 4);
          c.fillStyle = on ? INK : PAPER; c.fillRect(x + (fx + q) * px, y + (fy + r) * px, px, px);
        }
      };
      finder(0, 0); finder(N - 7, 0); finder(0, N - 7);
      for (let r = 0; r < 5; r++) for (let q = 0; q < 5; q++) {
        const on = r === 0 || r === 4 || q === 0 || q === 4 || (r === 2 && q === 2);
        c.fillStyle = on ? INK : PAPER; c.fillRect(x + (N - 9 + q) * px, y + (N - 9 + r) * px, px, px);
      }
    }

    /* ---------- modello ---------- */
    function count(s) { s.total = s.tasks.length; s.done = s.tasks.filter((t) => t[1] === 2).length; }
    function logEv(m, s, text, t) {
      m.log.unshift({ t: t || new Date(), name: s ? s.name : 'sistema', text: text });
      if (m.log.length > 80) m.log.length = 80;
    }
    function toast(m, msg, ms) { m.toast.msg = msg; m.toast.until = now() + (ms || 2600); }
    const isAttn = (s) => s.state === 'permission' || s.state === 'input' || s.state === 'error';
    // Archivia (2× nel Dettaglio): solo le sessioni ferme. Esce da m.sessions e va in m.archived finché non torna al lavoro.
    const canArchive = (s) => s.state === 'done' || s.state === 'idle' || s.state === 'error';
    const attention = (m) => m.sessions.some(isAttn);
    function urgentIdx(m) {
      let b = 0;
      m.sessions.forEach((s, i) => { if (STATES[s.state].prio > STATES[m.sessions[b].state].prio) b = i; });
      return b;
    }
    function sIdx(m) { return m.ui.selUntil > now() ? m.ui.sel : urgentIdx(m); }
    function unaIdx(m) {
      if (m.ui.selUntil > now()) return m.ui.sel;
      if (attention(m)) return urgentIdx(m);
      return m.ui.page % m.sessions.length;
    }
    /* ---------- Giornata di lavoro (firmware 0.9.0) ----------
     * m.work: orari predefiniti in minuti dalla mezzanotte (start, end: 09:00–18:00, impostazioni remote workStart e
     * workEnd), on, endAt = fine della giornata attiva (istante, fissa: regola A), from = attivazione, doneUntil =
     * «GIORNATA FINITA» a schermo fino a (un'ora dopo la fine, mai oltre la mezzanotte), party = festa fino a. */
    const WORK_DONE_MS = 3600000, WORK_PARTY_MS = 6000;
    const minHm = (mins) => pad2(Math.floor(mins / 60)) + ':' + pad2(mins % 60);
    function dayAt(t, mins) { const d = new Date(t); d.setHours(Math.floor(mins / 60), mins % 60, 0, 0); return d.getTime(); }
    function nextMidnight(t) { const d = new Date(t); d.setHours(24, 0, 0, 0); return d.getTime(); }
    const sameDay = (a, b) => new Date(a).toDateString() === new Date(b).toDateString();
    const workDone = (m) => m.work.doneUntil > now();
    // Minuti che mancano alla fine, arrotondati per eccesso: alle 9:40 di un giorno 9–18 fanno 8 h 20 (fine − orologio).
    function workLeftMin(m) { return Math.max(0, Math.ceil((m.work.endAt - now()) / 60000)); }
    // «6 h 25» / «25 min» (Home) e «6 H 25» / «25 MIN» (Riposo).
    function workLeftText(m, up) {
      const n = workLeftMin(m), s = n >= 60 ? Math.floor(n / 60) + ' h ' + pad2(n % 60) : n + ' min';
      return up ? s.toUpperCase() : s;
    }
    // Riga della Home accanto all'orologio: il conto, «FINITA» per un'ora dopo la fine, altrimenti niente.
    function workLine(m) { return m.work.on ? workLeftText(m, false) : workDone(m) ? 'finita' : ''; }
    // Barra della giornata: dall'orario d'inizio alla fine (se l'inizio non viene prima della fine: dall'attivazione).
    function workFrac(m) {
      const w = m.work;
      let a = dayAt(w.endAt, w.start);
      if (a >= w.endAt) a = w.from;
      return w.endAt > a ? clamp((now() - a) / (w.endAt - a), 0, 1) : 1;
    }
    const workPose = (m) => m.work.party > now();
    // Home S (Goccia, Contatori, Una alla volta): durante la festa di fine giornata (o di un traguardo del gioco) la
    // mascotte esulta, se la sessione mostrata non aspetta qualcuno (un permesso o una domanda restano visibili).
    const homePose = (m, s) => ((workPose(m) || petParty(m)) && !isAttn(s) ? 'done' : pose(m, s));

    /* ---------- Gioco della mascotte (firmware 0.10.0, specifica docs/superpowers/specs/2026-10-11-gioco-mascotte-design.md) ----------
     * m.set.game: interruttore (Menu › Display, impostazione remota `game`, spento di default). m.pet: l'ultimo messaggio
     * `game` del servizio (on, level, xp, xpLevelStart, xpNext, energy, tired, wear {id, crc} | null, unlocked [{id, name}])
     * più quello che tiene la scheda: goals = ultimi 3 traguardi (titoli dai `celebrate`, in RAM), lastGoal = id
     * dell'ultimo celebrate (lo stesso non rifà la festa), party = festa fino a. Il gioco conta solo con m.set.game e
     * pet.on: spento, niente accessori, feste né posa stanca dal gioco (i progressi restano sul PC). */
    const PET_PARTY_MS = 3000, PET_GOALS = 3, PET_UNLOCKED_MAX = 20;  // unlocked: al massimo 20 (PROTOCOLLO.md, `game`)
    const petOn = (m) => !!(m.set.game && m.pet.on);
    const petParty = (m) => petOn(m) && m.pet.party > now();
    // XP per passare dal livello n al n+1 (specifica §2): round(100 × n^1,3).
    const levelCost = (n) => Math.round(100 * Math.pow(n, 1.3));
    // La festa non parte con un permesso aperto, in silenzio (anche durante la concentrazione), di notte o in risparmio
    // energetico: lì solo il toast «Traguardo: …».
    const partyBlocked = (m) => m.sessions.some((s) => s.state === 'permission') || top(m).id === 'perm' || quiet(m) || m.set.night || powerSave(m);
    // Un traguardo nuovo (celebrate): in testa agli ultimi traguardi, toast di 3 s e, se si può, festa di 3 s.
    // Restituisce 'festa', 'toast' o '' (gioco spento o traguardo già festeggiato).
    function petCelebrate(m, id, title) {
      const p = m.pet;
      if (!petOn(m) || !id || id === p.lastGoal) return '';
      p.lastGoal = id;
      p.goals.unshift(title);
      if (p.goals.length > PET_GOALS) p.goals.length = PET_GOALS;
      toast(m, 'Traguardo: ' + title, PET_PARTY_MS);
      logEv(m, null, 'traguardo: ' + title);
      if (partyBlocked(m)) return 'toast';
      p.party = now() + PET_PARTY_MS;
      m.burstUntil = Math.max(m.burstUntil, p.party);
      return 'festa';
    }
    // Interruttore del gioco (m.set.game): la fonte è il servizio (game.on, docs/PROTOCOLLO.md «Impostazione game»). Il
    // menu e un `set` lo cambiano sulla scheda, ogni `game` lo riallinea. Spento: niente festa e via le schermate del gioco.
    function petSwitch(m, on) {
      m.set.game = on;
      if (on) return;
      m.pet.party = 0;
      while (m.ui.stack.length > 1 && (top(m).id === 'pet' || (top(m).id === 'list' && top(m).menu === 'petacc'))) m.ui.stack.pop();
    }
    // Messaggio `game` del servizio (sulla scheda Ui::netGame): sostituisce il riassunto, allinea l'interruttore a `on`
    // e poi festeggia il celebrate.
    function applyGame(m, g) {
      const p = m.pet;
      p.known = true; p.on = !!g.on; p.level = g.level; p.xp = g.xp; p.xpLevelStart = g.xpLevelStart; p.xpNext = g.xpNext;
      p.energy = g.energy; p.tired = !!g.tired;
      p.wear = g.wear ? { id: g.wear.id, crc: g.wear.crc } : null;
      p.unlocked = (g.unlocked || []).slice(0, PET_UNLOCKED_MAX).map((u) => ({ id: u.id, name: u.name }));
      if (m.set.game !== p.on) petSwitch(m, p.on);
      // special: c'è un accessorio speciale da chiedere a Claude. Toast una volta per accensione (specialSeen, in RAM):
      // se nello stesso messaggio c'è un traguardo, dopo il suo toast (specialToast, lo mostra tick()).
      p.special = !!g.special;
      if (p.special && petOn(m) && !p.specialSeen) { p.specialSeen = true; p.specialToast = true; }
      const r = g.celebrate ? petCelebrate(m, g.celebrate.id, g.celebrate.title) : '';
      if (p.specialToast && !r) { p.specialToast = false; toast(m, PET_SPECIAL_TOAST, 5000); }
      return r;
    }
    const PET_SPECIAL_TOAST = 'Nuovo accessorio speciale: chiedilo a Claude';
    // Il riassunto di adesso come messaggio `game` (per i pulsanti Simula, che fanno le veci del servizio).
    const petMsg = (p) => ({ on: p.on, level: p.level, xp: p.xp, xpLevelStart: p.xpLevelStart, xpNext: p.xpNext, energy: p.energy, tired: p.tired, special: p.special,
      wear: p.wear ? { id: p.wear.id, crc: p.wear.crc } : null, celebrate: null, unlocked: p.unlocked.map((u) => ({ id: u.id, name: u.name })) });
    const accName = (id) => { const u = ACCESSORIES[id]; return u ? u.name : id; };
    // Traguardi del pulsante Simula «Mascotte: traguardo» (id e titoli di service/internal/game/achievements.json).
    const PET_SIM_GOALS = [['primo-task', 'Primo task'], ['primo-verde', 'Primo verde'], ['task-10', '10 task'], ['serie-7', '7 giorni di serie'], ['livello-5', 'Livello 5']];
    // «La mascotte»: avanzamento nel livello (0..1) e testi.
    const petFrac = (p) => (p.xpNext > p.xpLevelStart ? clamp((p.xp - p.xpLevelStart) / (p.xpNext - p.xpLevelStart), 0, 1) : 0);

    /* ---------- Gioco «Alto o basso» (firmware 0.9.0) ----------
     * m.game: seed (xorshift32, fisso nel simulatore e nei test; sulla scheda esp_random), best (record, NVS),
     * card (1–13: A, 2…10, J, Q, K), prev, streak (serie), last ('' | 'win' | 'tie' | 'lose'), over, startBest. */
    const GAME_SEED = 0x2545F491;
    const RANKS = ['', 'A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K'];
    function gameDraw(m) {
      let s = m.game.seed >>> 0;
      s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0;
      m.game.seed = s;
      return 1 + (s % 13);
    }
    function gameNew(m) {
      const g = m.game;
      g.card = gameDraw(m); g.prev = 0; g.streak = 0; g.last = ''; g.over = false; g.startBest = g.best;
    }
    // 1× = più alta (up true), 2× = più bassa. Una carta uguale è pari: la serie resta com'è.
    function gameGuess(m, up) {
      const g = m.game, next = gameDraw(m);
      g.prev = g.card; g.card = next;
      if (next === g.prev) g.last = 'tie';
      else if ((next > g.prev) === up) { g.last = 'win'; g.streak++; if (g.streak > g.best) g.best = g.streak; }
      else { g.last = 'lose'; g.over = true; }
      return g.last;
    }
    const gamePose = (g) => (g.last === 'win' ? 'done' : g.last === 'lose' ? 'error' : g.last === 'tie' ? 'idle' : 'input');

    const muted = (m) => m.set.mute > now();
    const focusOn = (m) => m.focus.until > now();
    const quiet = (m) => muted(m) || focusOn(m);
    function extras(s) {
      const a = [];
      if (s.subs) a.push('+' + s.subs + (s.subs === 1 ? ' agente' : ' agenti'));
      if (s.test) a.push(s.test === 'ok' ? 'test ✓' : 'test ✗');
      if (s.ctx >= 80) a.push('contesto ' + s.ctx + '%');
      return a;
    }
    function lineFor(s) { return s.state === 'done' && s.summary ? s.summary : s.detail; }
    function stateText(s) { return s.state === 'working' ? s.detail : (s.state === 'done' && s.summary ? s.summary : STATES[s.state].long); }
    function metaLine(s) { return [s.total ? s.done + '/' + s.total + ' task' : 'senza task', ago(s.since)].concat(extras(s), [lineFor(s)]).join('  ·  '); }
    function pose(m, s) {
      if (m.limits && m.limits.h5 >= 90 && (s.state === 'working' || s.state === 'done' || s.state === 'idle')) return 'tired';
      // Gioco: dopo 3 ore senza pausa saggia la mascotte è stanca, nelle stesse pose dei limiti.
      if (petOn(m) && m.pet.tired && (s.state === 'working' || s.state === 'done' || s.state === 'idle')) return 'tired';
      if (m.acc && m.air.co2 > 1000 && !isAttn(s)) return 'stuffy';
      return s.state;
    }
    const powerSave = (m) => m.set.save || (m.sys.batt < 15 && !m.sys.usb);
    /* ---------- M6: batteria, calibrazione, autonomia ---------- */
    // Curva a vuoto della LiPo (firmware core/battery: OCV di Meshtastic), dal 100% allo 0% a passi del 10%.
    const OCV = [4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100];
    // Percentuale continua dalla tensione e tensione dalla percentuale (interpolazione lineare sulla curva).
    function pctFromMv(mv) {
      if (mv >= OCV[0]) return 100;
      if (mv <= OCV[10]) return 0;
      for (let k = 0; k < 10; k++) if (mv >= OCV[k + 1]) return 100 - 10 * k - 10 * (OCV[k] - mv) / (OCV[k] - OCV[k + 1]);
      return 0;
    }
    function mvFromPct(p) {
      if (p >= 100) return OCV[0];
      if (p <= 0) return OCV[10];
      const k = Math.floor((100 - p) / 10), f = (100 - 10 * k - p) / 10;
      return Math.round(OCV[k] - f * (OCV[k] - OCV[k + 1]));
    }
    // Calibrazione: due fasi da 60 minuti (massimo consumo, attesa), una lettura al minuto; serve il 90%.
    const CAL_PHASE_MS = 3600000, CAL_EVERY_MS = 60000, CAL_MIN_PCT = 90, CAL_MIN_SAMPLES = 5;
    // Sonno profondo non ancora misurato dalle notti: valore prudente (stimato, vedi DECISIONI M6).
    const SLEEP_RATE_DEF = 0.3;
    // Consumo in %/h da una fase: pendenza (minimi quadrati) della percentuale nel tempo, cambiata di segno.
    function calRate(samples) {
      const n = samples.length;
      if (n < CAL_MIN_SAMPLES) return null;
      let sx = 0, sy = 0, sxx = 0, sxy = 0;
      samples.forEach((p) => { sx += p[0]; sy += p[1]; sxx += p[0] * p[0]; sxy += p[0] * p[1]; });
      const d = n * sxx - sx * sx;
      if (d <= 0) return null;
      return -(n * sxy - sx * sy) / d;
    }
    const fmtRate = (r) => fmt1(Math.max(0, r)) + ' %/h';
    const fmtV = (mv) => (Math.round(mv / 10) / 100).toFixed(2).replace('.', ',') + ' V';
    // Consumo medio pesato con i tempi passati in Attivo, Attesa e Sonno nelle ultime 24 ore (m.bat.mix).
    function battRate(m) {
      const c = m.bat.cal;
      if (!c) return 0;
      const x = m.bat.mix, sl = m.bat.sleep != null ? m.bat.sleep : SLEEP_RATE_DEF, tot = x[0] + x[1] + x[2];
      return tot > 0 ? (x[0] * c.active + x[1] * c.idle + x[2] * sl) / tot : 0;
    }
    function hoursText(h) { return h >= 48 ? 'circa ' + Math.round(h / 24) + ' giorni' : 'circa ' + Math.max(1, Math.round(h)) + ' ore'; }
    function autonomyText(m) {
      const r = battRate(m);
      if (m.bat.cal && r > 0.01) return 'Autonomia: ' + hoursText(m.sys.batt / r) + ' con il tuo uso';
      if (!m.sys.usb && m.bat.trend > 0.01) return 'Autonomia: ' + hoursText(m.sys.batt / m.bat.trend) + ' (dalla scarica)';
      return m.sys.usb ? 'Autonomia: calibra per una stima' : 'Autonomia: stima in corso';
    }
    const calRunning = (m) => !!m.calib && m.calib.phase !== 'done';
    function calDate(m) { const d = new Date(m.bat.cal.at); return pad2(d.getDate()) + '/' + pad2(d.getMonth() + 1); }
    // M5: si risponde ai permessi dal display solo se è abbinato (sul firmware con la rete: dopo auth-ok).
    const permFromDisplay = (m) => m.set.permOn && m.sys.pair === 'paired';
    // Nessuna scadenza: al posto dei secondi rimasti, i minuti interi passati dall'arrivo della richiesta.
    function permWaitText(since) {
      const mins = Math.floor(Math.max(0, now() - since) / 60000);
      if (mins < 1) return 'ARRIVATA ORA';
      if (mins < 60) return 'IN ATTESA DA ' + mins + ' MIN';
      return 'IN ATTESA DA ' + Math.floor(mins / 60) + ' H ' + pad2(mins % 60);
    }
    function permSince(m, tp) { const s = m.sessions[tp.i]; return s ? (s.attnAt || s.since) : now(); }
    function pairLabel(m) { const y = m.sys; return y.pair === 'redo' ? 'da rifare' : y.pair === 'none' ? 'non abbinato' : (y.pairHost ? 'abbinato a ' + y.pairHost : 'abbinato'); }
    function pairShort(m) { const y = m.sys; return y.pair === 'redo' ? 'da rifare' : y.pair === 'none' ? 'non abbinato' : 'abbinato'; }
    const defaultSet = () => ({ homeStyle: 'goccia', anim: 'auto', speed: 'profilo', invert: false, rot: false, mute: 0, muteKind: '', night: false, permOn: true, sound: true, led: true, save: false, hookDirect: true, defaultAvatar: 'goccia', game: false });
    const top = (m) => m.ui.stack[m.ui.stack.length - 1];
    function addWait(m, s) { if (s.attnAt) { m.stats.waitSum += now() - s.attnAt; m.stats.waitN++; s.attnAt = 0; } }
    function rssiBars(r) { return r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0; }
    function newModel(dev) {
      const t = now(), ago_ = (mm) => t - mm * 60000;
      const S = [
        { name: 'api-server', avatar: 'gatto', state: 'working', detail: 'Edit src/routes/orders.ts', since: ago_(12), ctx: 46, subs: 2, test: null, summary: '', tasks: [['Leggi le route esistenti', 2], ['Endpoint POST /orders', 2], ['Validazione input', 2], ['Test di integrazione', 1], ['Aggiorna OpenAPI', 0], ['Errori 4xx coerenti', 0], ['Review finale', 0]] },
        { name: 'web-app', avatar: 'demone', state: 'permission', detail: 'Bash: npm run test', since: ago_(26), attnAt: ago_(1), ctx: 71, subs: 0, test: 'ko', summary: '', tasks: [['Pagina carrello', 2], ['Hook useCart', 2], ['Stato globale', 2], ['Stili mobile', 2], ['Fix layout tablet', 2], ['Esegui i test', 1], ['Correggi i test rotti', 0], ['Controllo Lighthouse', 0], ['Changelog', 0]] },
        { name: 'firmware-esp32', avatar: 'calamaro', state: 'working', detail: 'Read lib/epd/driver.cpp', since: ago_(4), ctx: 18, subs: 1, test: 'ok', summary: '', tasks: [['Driver SSD1683', 2], ['Layout 400×300', 1], ['Sprite mascotte', 0], ['Client WebSocket', 0]] },
        { name: 'docs-site', state: 'done', detail: '6 task completati', since: ago_(41), ctx: 83, subs: 0, test: 'ok', summary: 'Guida di installazione aggiornata e 4 link rotti corretti.', tasks: [['Indice', 2], ['Guida installazione', 2], ['Esempi API', 2], ['Screenshot', 2], ['Glossario', 2], ['Link rotti', 2]] }
      ];
      S.forEach(count);
      const m = {
        sessions: S, log: [], archived: [],
        ui: { stack: [{ id: 'boot', until: t + 2600 }], sel: 1, selUntil: 0, page: 0, forgetUntil: 0, forgetNetUntil: 0, unpairUntil: 0, newBattUntil: 0, hold: null },
        set: defaultSet(),
        sys: { wifi: true, ssid: 'CasaWiFi', rssi: -58, ip: '192.168.1.42', pc: true, lastPc: t, batt: 78, usb: false, temp: 22.4, rh: 48, bootAt: t, pcOff: false, offAt: 0, apPass: 'K7MZ3QRT', pair: 'paired', pairHost: 'calamaio.local', keys: false, wallOk: true },
        // Reti salvate nell'ordine di inserimento (Menu › Wi-Fi › Reti salvate): used = ultimo collegamento (0 = mai).
        // CasaWiFi e Officina sono tra le reti vicine finte (NETS), le altre due non si vedono.
        saved: [{ ssid: 'CasaWiFi', used: t }, { ssid: 'Officina', used: t - 3 * 86400000 }, { ssid: 'Casa Mare', used: t - 64 * 86400000 }, { ssid: 'Laboratorio', used: 0 }],
        stats: { tasks: 14, permDisplay: 2, permTerminal: 3, waitSum: 11 * 60000, waitN: 5, linesAdd: 1240, linesDel: 380, cost: 18.4 },
        limits: { h5: 62, h5Reset: t + 130 * 60000, d7: 41, d7Reset: t + (3 * 24 + 4) * 3600000 },
        focus: { until: 0, len: 25 },
        acc: false,
        air: { co2: 820 },
        services: [{ name: 'Sito pubblico', ok: true, ms: 120 }, { name: 'API', ok: true, ms: 85 }, { name: 'Backup notturno', ok: false, ms: 0 }],
        prs: [{ repo: 'web-app', n: 42, title: 'Carrello su mobile', checks: 'ko' }, { repo: 'api-server', n: 17, title: 'Endpoint ordini', checks: 'run' }, { repo: 'docs-site', n: 8, title: 'Guida installazione', checks: 'ok' }],
        agenda: [[50, 'Riunione di progetto'], [165, 'Chiamata con un fornitore'], [1500, 'Revisione del codice']].map((a) => { const d = new Date(t + a[0] * 60000); d.setMinutes(Math.round(d.getMinutes() / 15) * 15, 0, 0); return { t: d.getTime(), title: a[1] }; }),
        toast: { msg: '', until: 0 },
        burstUntil: t + 6000, nextBurst: t + 30000,
        // M6: calibrazione salvata (%/h in Attivo e in Attesa, data), sonno dalle notti (%/h), quota del tempo nei tre
        // stati nelle ultime 24 ore (sul firmware dal registro dei tempi; qui un uso tipico), pendenza della scarica (%/h).
        bat: { cal: null, sleep: null, mix: [30, 50, 20], trend: 0 },
        calib: null,
        work: { start: 9 * 60, end: 18 * 60, on: false, endAt: 0, from: 0, doneUntil: 0, party: 0 },
        game: { seed: GAME_SEED, best: 0, card: 0, prev: 0, streak: 0, last: '', over: false, startBest: 0 },
        // Gioco della mascotte: un riassunto finto come se fosse arrivato dal servizio (livello 3, il cappellino sbloccato al 2),
        // con il gioco spento come l'interruttore (Simula «Gioco della mascotte (dal PC)» lo accende).
        pet: { known: true, on: false, level: 3, xp: 412, xpLevelStart: 346, xpNext: 763, energy: 80, tired: false, wear: null,
          unlocked: [{ id: 'cappellino', name: 'Cappellino' }], goals: [], lastGoal: '', party: 0, special: false, specialSeen: false, specialToast: false },
        ota: null  // aggiornamento remoto: { phase, from, to, done, total, why, step, until } (docs/OTA.md)
      };
      const L = [[118, 3, 'sessione aperta'], [96, 0, 'sessione aperta'], [80, 3, 'task 1/6 completato'], [64, 3, 'task 3/6 completato'], [41, 3, 'turno completato'], [33, 1, 'sessione aperta'], [26, 1, 'nuovo prompt, 9 task'], [12, 0, 'nuovo prompt, 7 task'], [9, 0, 'task 2/7 completato'], [7, 1, 'task 5/9 completato'], [4, 2, 'sessione aperta'], [3, 2, 'task 1/4 completato'], [2, 0, 'task 3/7 completato'], [1, 1, 'chiede permesso: npm run test']];
      L.forEach((r) => logEv(m, S[r[1]], r[2], new Date(ago_(r[0]))));
      return m;
    }

    /* ---------- animazioni e refresh ---------- */
    function profileName(dev) { return dev.fps >= 3 ? 'fluide' : dev.fps === 2 ? 'ridotte' : 'a pose'; }
    function animOff(m) { return m.set.anim === 'spente' || m.set.night || powerSave(m); }
    const ANIM_SCREENS = { home: 1, detail: 1, perm: 1, connecting: 1, sleep: 1, boot: 1, focus: 1, ambiente: 1, calib: 1, game: 1 };
    // Velocità animazioni «Massima»: aggiorna appena il pannello è pronto (fpsMax misurato) con 4 fotogrammi.
    const maxSpeed = (m) => m.set.speed === 'massima';
    const effFps = (dev, m) => maxSpeed(m) ? (dev.fpsMax || dev.fps) : dev.fps;
    function animRate(dev, m, t) {
      const tp = top(m), fps = effFps(dev, m);
      if (!m.sys.pc && (tp.id === 'home' || tp.id === 'detail')) return 0;
      if (!ANIM_SCREENS[tp.id]) return 0;
      if (tp.id === 'connecting' || tp.id === 'boot') return Math.max(1, fps);
      // Calibrazione, fase 1: deve consumare davvero (animazione continua alla velocità massima); fase 2 ferma.
      if (tp.id === 'calib') return m.calib && m.calib.phase === 'max' && !m.calib.paused ? (dev.fpsMax || dev.fps) : 0;
      if (animOff(m)) return 0;
      const call = attention(m) && !quiet(m);
      if (dev.fps <= 1 && !maxSpeed(m)) return call ? 0.5 : 0;
      if (m.set.anim === 'continue') return fps;
      if (t < m.burstUntil) return fps;
      if (m.set.anim === 'auto' && call) return fps;
      return 0;
    }
    function frameFor(dev, m, f, t) {
      // Risparmio energetico: Goccia ferma sul fotogramma 0 della sua posa (avvio e connessione restano animati).
      const id = top(m).id;
      if (id === 'calib') return m.calib && m.calib.phase === 'max' ? f : 0;
      if (powerSave(m) && id !== 'boot' && id !== 'connecting') return 0;
      if (dev.fps >= 3 || maxSpeed(m)) return f;
      if (dev.fps === 2) return f % 2;
      return (attention(m) && !quiet(m) && !animOff(m)) ? Math.floor(t / 2000) % 2 : 0;
    }
    function profileText(dev, m) {
      return dev.fps + ' agg./s (stima) · completo ogni ' + dev.fullEvery + ' · animazioni ' + profileName(dev) + ' · modalità ' + ANIM_LABEL[m.set.anim].toLowerCase() + (animOff(m) ? ' (ferme ora)' : '');
    }

    /* ---------- elementi comuni ---------- */
    function hintSet(dev, ctx) {
      const i = dev.input;
      if (i === 'single') return ({ home: [['1×', 'scegli'], ['2×', 'apri'], ['3×', 'menu'], ['LUNGA', 'silenzia']], detail: [['1×', 'sessione'], ['2×', 'rispondi'], ['LUNGA', 'indietro']], list: [['1×', 'avanti'], ['2×', 'scegli'], ['LUNGA', 'indietro']], perm: [['1×', 'cambia'], ['2×', 'conferma'], ['LUNGA', 'annulla']], view: [['LUNGA', 'indietro'], ['3×', 'home']], scroll: [['1×', 'scorri'], ['LUNGA', 'indietro']], qr: [['2×', 'simula password'], ['LUNGA', 'indietro']], batt: [['2×', 'risparmio'], ['LUNGA', 'indietro']], focus: [['2×', 'avvia o ferma'], ['LUNGA', 'indietro']] })[ctx];
      if (i === 'wheel') return ({ home: [['ud', 'scegli'], ['ok', 'apri'], ['MENU', 'menu']], detail: [['ud', 'sessione'], ['ok', 'rispondi'], ['EXIT', 'indietro']], list: [['ud', 'scegli'], ['ok', 'conferma'], ['EXIT', 'indietro']], perm: [['ud', 'scegli'], ['ok', 'conferma'], ['EXIT', 'annulla']], view: [['EXIT', 'indietro'], ['MENU', 'home']], scroll: [['ud', 'scorri'], ['EXIT', 'indietro']], qr: [['ok', 'simula password'], ['EXIT', 'indietro']], batt: [['ok', 'risparmio'], ['EXIT', 'indietro']], focus: [['ok', 'avvia o ferma'], ['EXIT', 'indietro']] })[ctx];
      return ({ home: [['lr', 'scegli'], ['ok', 'apri'], ['TIENI OK', 'menu']], detail: [['lr', 'sessione'], ['ok', 'rispondi'], ['TIENI ◀', 'indietro']], list: [['lr', 'scegli'], ['ok', 'conferma'], ['TIENI ◀', 'indietro']], perm: [['lr', 'scegli'], ['ok', 'conferma'], ['TIENI ◀', 'annulla']], view: [['TIENI ◀', 'indietro'], ['TIENI OK', 'menu']], scroll: [['lr', 'scorri'], ['TIENI ◀', 'indietro']], qr: [['ok', 'simula password'], ['TIENI ◀', 'indietro']], batt: [['ok', 'risparmio'], ['TIENI ◀', 'indietro']], focus: [['ok', 'avvia o ferma'], ['TIENI ◀', 'indietro']] })[ctx];
    }
    function hints(c, dev, items) {
      const mt = MET(dev);
      if (!mt.hint || !items) return;
      const h = mt.hint, y0 = dev.H - h, s = h >= 26 ? 2 : 1, fs = Math.round(h * 0.55), ty = y0 + Math.round((h - fs) / 2), cy = y0 + Math.round(h / 2);
      c.fillStyle = PAPER; c.fillRect(0, y0, dev.W, h);
      dotsH(c, 0, dev.W, y0 + 1);
      let x = mt.pad;
      items.forEach((it) => {
        const k = it[0], t = it[1];
        if (k === 'ud') { tri(c, x, cy - 5 * s, s, 'up'); tri(c, x, cy + s, s, 'down'); x += 11 * s; }
        else if (k === 'lr') { tri(c, x, cy - 4 * s, s, 'left'); tri(c, x + 6 * s, cy - 4 * s, s, 'right'); x += 14 * s; }
        else if (k === 'ok') { glyph(c, G.dot, x, cy - 4 * s, s, INK); x += 11 * s; }
        else {
          const cf = Math.max(8, fs - 2);
          c.font = '700 ' + cf + 'px ' + F_UI;
          const tw = Math.ceil(c.measureText(k).width);
          c.fillStyle = INK; c.fillRect(x, cy - Math.round(cf / 2) - 2 * s, tw + 8 * s, cf + 4 * s);
          T(c, k, x + 4 * s, cy - Math.round(cf / 2), cf, { w: 700, c: PAPER });
          x += tw + 12 * s;
        }
        T(c, t, x, ty, fs, { w: 600 });
        c.font = '600 ' + fs + 'px ' + F_UI;
        x += Math.ceil(c.measureText(t).width) + 14 * s;
      });
    }
    function statusBar(c, dev, m, title, back, hit, ctl) {
      const mt = MET(dev), h = mt.sb;
      if (!h) return 0;
      const W = dev.W, s = h >= 30 ? 2 : 1, fs = Math.round(h * 0.56), ty = Math.round((h - fs) / 2) - 1, bg = dev.gray ? '#262626' : INK, iy7 = Math.round((h - 7 * s) / 2);
      c.fillStyle = bg; c.fillRect(0, 0, W, h);
      let x = Math.round(h * 0.35);
      if (back) {
        chevL(c, x, Math.round((h - 9 * s) / 2), PAPER, s);
        if (hit && ctl) hit(0, 0, x + 12 * s, h, ctl.back);
        x += 6 * s + Math.round(h * 0.3);
      }
      let r = W - Math.round(h * 0.35);
      c.font = '600 ' + fs + 'px ' + F_UI;
      const tc = clock(m);
      T(c, tc, r, ty, fs, { w: 600, c: PAPER, a: 'right' });
      c.font = '600 ' + fs + 'px ' + F_UI;
      r -= Math.ceil(c.measureText(tc).width) + 9 * s;
      const pct = Math.round(m.sys.batt) + '%';
      T(c, pct, r, ty, fs, { w: 600, c: PAPER, a: 'right' });
      c.font = '600 ' + fs + 'px ' + F_UI;
      r -= Math.ceil(c.measureText(pct).width) + 3 * s;
      r -= 16 * s; battIcon(c, r, Math.round((h - 8 * s) / 2), s, m.sys.batt, PAPER);
      if (m.sys.usb) { r -= 7 * s; glyph(c, G.bolt, r, iy7, s, PAPER); }
      r -= 9 * s;
      r -= 11 * s; wifiIcon(c, r, Math.round((h - 9 * s) / 2), s, m.sys.wifi ? rssiBars(m.sys.rssi) : -1, PAPER);
      r -= 8 * s;
      r -= 7 * s; glyph(c, G.plug, r, iy7, s, PAPER);
      if (!m.sys.pc) strike(c, r - s, iy7 - s, 9, s, PAPER, bg);
      r -= 8 * s;
      if (muted(m)) { r -= 7 * s; glyph(c, G.bell, r, iy7, s, PAPER); strike(c, r - s, iy7 - s, 9, s, PAPER, bg); r -= 8 * s; }
      if (m.set.night) { r -= 7 * s; glyph(c, G.moon, r, iy7, s, PAPER); r -= 8 * s; }
      if (focusOn(m)) { r -= 7 * s; glyph(c, G.glass, r, iy7, s, PAPER); r -= 8 * s; }
      if (dev.sensor) {
        const tt = fmt1(m.sys.temp) + '°  ' + Math.round(m.sys.rh) + '%';
        T(c, tt, r, ty, fs, { w: 600, c: PAPER, a: 'right' });
        c.font = '600 ' + fs + 'px ' + F_UI;
        r -= Math.ceil(c.measureText(tt).width) + 10 * s;
      }
      if (m.acc) {
        const ct = 'CO2 ' + Math.round(m.air.co2);
        T(c, ct, r, ty, fs, { w: m.air.co2 > 1000 ? 700 : 600, c: PAPER, a: 'right' });
        c.font = '600 ' + fs + 'px ' + F_UI;
        r -= Math.ceil(c.measureText(ct).width) + 10 * s;
      }
      if (m.limits && m.limits.h5 != null) {
        const lt = '5h ' + Math.round(m.limits.h5) + '%';
        T(c, lt, r, ty, fs, { w: m.limits.h5 >= 80 ? 700 : 600, c: PAPER, a: 'right' });
        c.font = '600 ' + fs + 'px ' + F_UI;
        r -= Math.ceil(c.measureText(lt).width) + 10 * s;
      }
      T(c, title, x, ty, fs, { w: 700, c: PAPER, max: Math.max(40, r - x - 10) });
      return h;
    }
    // battShown: la batteria è già in alto accanto all'orologio (battS), qui non si ripete.
    function alertsS(c, m, xr, y, battShown) {
      let x = xr;
      const put = (g, struck) => { x -= 7; glyph(c, g, x, y, 1, INK); if (struck) strike(c, x - 1, y - 1, 9, 1, INK, PAPER); x -= 4; };
      if (!m.sys.pc) put(G.plug, true);
      if (!m.sys.wifi) { x -= 11; wifiIcon(c, x, y - 1, 1, -1, INK); x -= 4; }
      if (m.sys.batt < 15 && !m.sys.usb && !battShown) { x -= 16; battIcon(c, x, y, 1, m.sys.batt, INK); x -= 4; }
      if (muted(m)) put(G.bell, true);
      if (m.set.night) put(G.moon, false);
      if (focusOn(m)) put(G.glass, false);
      if (m.limits && m.limits.h5 >= 80) put(G.gauge, false);
      return x;
    }
    // Batteria accanto all'orologio (classe S): da destra «78%» in Silkscreen 8, 3 px, icona 14×8 con la
    // punta; in carica il fulmine a sinistra dell'icona (come statusBar). xr = bordo destro, y = quello
    // dell'orologio. Restituisce il bordo sinistro.
    function battS(c, m, xr, y, col) {
      const pct = Math.round(m.sys.batt) + '%';
      T(c, pct, xr, y, 8, { f: F_PIX, w: 400, a: 'right', c: col });
      c.font = '400 8px ' + F_PIX;
      let x = xr - Math.ceil(c.measureText(pct).width) - 3 - 16;
      battIcon(c, x, y, 1, m.sys.batt, col);
      if (m.sys.usb) { x -= 7; glyph(c, G.bolt, x, y, 1, col); }
      return x;
    }
    // Giornata di lavoro (0.9.0): «6 h 25» in Silkscreen 8 a sinistra degli avvisi, sulla riga dell'orologio (Goccia,
    // Contatori, Home vuota). xr = bordo sinistro degli avvisi (alertsS), xmin = dove può arrivare al massimo: se non ci
    // sta (molti avvisi) non si disegna. Restituisce il nuovo bordo sinistro della riga.
    function workS(c, m, xr, y, xmin) {
      const t = workLine(m);
      if (!t) return xr;
      c.font = '400 8px ' + F_PIX;
      const x = xr - 2 - Math.ceil(c.measureText(t).width);
      if (x < xmin) return xr;
      T(c, t, xr - 2, y, 8, { f: F_PIX, w: 400, a: 'right' });
      return x - 4;
    }
    const hasAlerts = (m) => !m.sys.pc || !m.sys.wifi || (m.sys.batt < 15 && !m.sys.usb) || muted(m) || m.set.night || focusOn(m) || (m.limits && m.limits.h5 >= 80);
    function drawToast(c, dev, m) {
      if (!m.toast.msg || now() > m.toast.until) return;
      const mt = MET(dev), S = dev.cls === 'S', fs = S ? 10 : Math.max(11, mt.f3), px = S ? 8 : mt.pad;
      c.font = '700 ' + fs + 'px ' + F_UI;
      const tw = Math.min(dev.W - 20, Math.ceil(c.measureText(m.toast.msg).width) + px * 2), x = Math.round((dev.W - tw) / 2), h = fs + (S ? 8 : mt.pad), y = dev.H - mt.hint - h - (S ? 4 : mt.pad);
      c.fillStyle = PAPER; c.fillRect(x - 2, y - 2, tw + 4, h + 4);
      c.fillStyle = INK; c.fillRect(x, y, tw, h);
      T(c, m.toast.msg, dev.W / 2, y + Math.round((h - fs) / 2), fs, { w: 700, a: 'center', c: PAPER, max: tw - 8 });
    }
    function banner(c, dev, m) {
      const s = m.sessions[urgentIdx(m)];
      if (!s || !isAttn(s)) return;
      const mt = MET(dev), h = mt.f3 + 10, y = dev.H - mt.hint - h - 2;
      c.fillStyle = PAPER; c.fillRect(0, y - 2, dev.W, h + 4);
      c.fillStyle = INK; c.fillRect(0, y, dev.W, h);
      T(c, '!  ' + s.name + ' · ' + STATES[s.state].long.toLowerCase(), mt.pad, y + 5, mt.f3, { w: 700, c: PAPER, max: dev.W - mt.pad * 2 });
    }
    function stripS(c, dev, m, title, hit, ctl) {
      c.fillStyle = INK; c.fillRect(0, 0, dev.W, 14);
      chevL(c, 4, 2, PAPER, 1);
      const tc = clock(m);
      c.font = '400 8px ' + F_PIX;
      const bx = battS(c, m, dev.W - 4 - Math.ceil(c.measureText(tc).width) - 6, 3, PAPER);
      T(c, title.toUpperCase(), 13, 3, 8, { f: F_PIX, w: 700, c: PAPER, max: Math.max(40, bx - 13 - 6) });
      T(c, tc, dev.W - 4, 3, 8, { f: F_PIX, w: 400, a: 'right', c: PAPER });
      if (hit && ctl) hit(0, 0, 40, 14, ctl.back);
    }
    function viewHeader(c, dev, m, title, hit, ctl) {
      if (dev.cls === 'S') { stripS(c, dev, m, title, hit, ctl); return 16; }
      return statusBar(c, dev, m, title, true, hit, ctl) + MET(dev).pad;
    }
    function drawVal(c, it, rx, y, fs, col, s) {
      let x = rx;
      if (it.go || it.open) {
        c.font = '700 ' + (fs + 2) + 'px ' + F_UI;
        const w = Math.ceil(c.measureText('›').width);
        T(c, '›', x, y - 2, fs + 2, { w: 700, c: col, a: 'right' });
        x -= w + 6;
      }
      if (it.bars !== undefined) {
        x -= 11 * s; wifiIcon(c, x, y + Math.round((fs - 9 * s) / 2), s, it.bars, col); x -= 5 * s;
        if (it.lock) { x -= 7 * s; glyph(c, G.lock, x, y + Math.round((fs - 7 * s) / 2), s, col); x -= 5 * s; }
      }
      if (it.val) T(c, it.val, x, y, fs, { w: 600, c: col, a: 'right', max: Math.max(40, Math.round(rx * 0.45)) });
    }
    function scrollbar(c, x, y, h, off, vis, total) {
      if (total <= vis) return;
      dotsV(c, x + 1, y, y + h);
      const th = Math.max(8, Math.round(h * vis / total)), ty = y + Math.round((h - th) * (off / Math.max(1, total - vis)));
      c.fillStyle = INK; c.fillRect(x, ty, 3, th);
    }

    /* ---------- Home ---------- */
    function homeGoccia(c, dev, m, f, hit, ctl) {
      const W = dev.W, i = sIdx(m), s = m.sessions[i];
      figure(c, m, s, 4, 10, 4, homePose(m, s), f);
      if (petParty(m) && !isAttn(s)) partyFx(c, m, s, 4, 10, 4, f);
      const x = 120, w = W - x - 6;
      T(c, clock(m), W - 6, 4, 8, { f: F_PIX, w: 400, a: 'right' });
      c.font = '400 8px ' + F_PIX;
      workS(c, m, alertsS(c, m, battS(c, m, W - 6 - Math.ceil(c.measureText(clock(m)).width) - 6, 4, INK) - 6, 4, true), 4, x);
      T(c, STATES[s.state].label, x, 18, 20, { w: 700, max: w });
      T(c, s.name, x, 44, 13, { w: 600, max: w });
      T(c, stateText(s), x, 61, 10, { w: 500, max: w });
      bar(c, x, 78, w, 8, s, f);
      m.sessions.forEach((t, k) => {
        const ix = x + k * 14;
        icon(c, t.state, ix, 101, 1, f);
        if (k === i) { c.fillStyle = INK; c.fillRect(ix, 111, 7, 2); }
        if (hit) hit(ix - 3, 96, 14, 20, () => ctl.openSession(k));
      });
      T(c, ago(s.since), W - 6, 101, 8, { f: F_PIX, w: 400, a: 'right' });
      if (hit) hit(0, 0, 118, 122, () => ctl.openSession(i));
    }
    function homeRighe(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, pin = m.ui.selUntil > now() ? m.ui.sel : -1;
      m.sessions.forEach((s, i) => {
        const y = i * 30 + 1, a = isAttn(s), col = a ? PAPER : INK;
        if (a) { c.fillStyle = INK; c.fillRect(0, y, W, 17); }
        icon(c, s.state, 6, y + 2, 2, f, col);
        T(c, s.name, 26, y + 1, 13, { w: 700, c: col, max: 150 });
        T(c, a ? STATES[s.state].label : (s.total ? s.done + '/' + s.total : ''), W - 6, y + 3, 10, { w: 700, a: 'right', c: col });
        bar(c, 6, y + 19, W - 12, 7, s, f);
        if (i === pin) { c.fillStyle = INK; c.fillRect(0, y + 19, 3, 7); c.fillRect(W - 3, y + 19, 3, 7); }
        if (hit) hit(0, y, W, 28, () => ctl.openSession(i));
      });
      if (hasAlerts(m)) { c.fillStyle = PAPER; c.fillRect(W - 64, H - 12, 64, 12); alertsS(c, m, W - 3, H - 10); }
    }
    function homeNumeri(c, dev, m, f, hit, ctl) {
      const W = dev.W, S = m.sessions;
      const lav = S.filter((s) => s.state === 'working').length, att = S.filter(isAttn).length, fin = S.filter((s) => s.state === 'done' || s.state === 'idle').length;
      const cols = [['LAVORANO', lav, false], ['PER TE', att, att > 0], ['FINITE', fin, false]], cw = Math.floor(W / 3);
      cols.forEach((cl, i) => {
        const x = i * cw, cx = x + cw / 2, inv = cl[2], col = inv ? PAPER : INK;
        if (inv) { c.fillStyle = INK; c.fillRect(x + 3, 3, cw - 6, 74); }
        T(c, String(cl[1]), cx, 6, 46, { w: 700, a: 'center', c: col });
        T(c, cl[0], cx, 62, 8, { f: F_PIX, w: 700, a: 'center', c: col, max: cw - 8 });
        if (i < 2 && !inv && !cols[i + 1][2]) dotsV(c, x + cw, 8, 74);
      });
      dotsH(c, 0, W, 83);
      const i = sIdx(m), s = S[i];
      figure(c, m, s, 3, 90, 1, homePose(m, s), f);
      if (petParty(m) && !isAttn(s)) partyFx(c, m, s, 3, 90, 1, f);
      T(c, clock(m), W - 6, 90, 8, { f: F_PIX, w: 400, a: 'right' });
      c.font = '400 8px ' + F_PIX;
      const ax = workS(c, m, alertsS(c, m, battS(c, m, W - 6 - Math.ceil(c.measureText(clock(m)).width) - 6, 90, INK) - 6, 90, true), 90, 36 + 60 + 4);
      T(c, s.name, 36, 88, 12, { w: 700, max: Math.max(40, ax - 36 - 4) });
      T(c, isAttn(s) ? STATES[s.state].label + ' · ' + s.detail : stateText(s), 36, 105, 10, { w: 500, max: W - 44 });
      if (hit) hit(0, 84, W, 38, () => ctl.openSession(i));
    }
    function homeUna(c, dev, m, f, hit, ctl) {
      const W = dev.W, i = unaIdx(m), s = m.sessions[i], a = isAttn(s);
      T(c, s.name, 8, 5, 15, { w: 700, max: 150 });
      if (a) { c.fillStyle = INK; c.fillRect(4, 25, 152, 27); }
      T(c, STATES[s.state].label, 8, 26, 22, { w: 700, c: a ? PAPER : INK, max: 146 });
      wrapT(c, lineFor(s), 8, 57, 11, 150, 2, 13, { w: 500 });
      figure(c, m, s, 163, 4, 3, homePose(m, s), f);
      if (petParty(m) && !a) partyFx(c, m, s, 163, 4, 3, f);
      bar(c, 8, 88, W - 16, 10, s, f);
      T(c, (s.total ? s.done + '/' + s.total + ' task' : 'senza task') + ' · ' + ago(s.since), 8, 104, 10, { w: 600, max: 140 });
      m.sessions.forEach((t, k) => {
        const x = W - 8 - (m.sessions.length - k) * 10 + 4;
        if (k === i) { c.fillStyle = INK; c.fillRect(x, 106, 6, 6); }
        else { box(c, x, 106, 6, 6, INK, 1); if (isAttn(t)) { c.fillStyle = INK; c.fillRect(x + 2, 108, 2, 2); } }
      });
      alertsS(c, m, W - 8 - m.sessions.length * 10 - 4, 105);
      if (hit) hit(0, 0, W, 100, () => ctl.openSession(i));
    }
    // Nessuna sessione aperta sul PC. Classe S: stessi testi e posizioni del firmware (homeEmpty, M3).
    // M, W, L: impaginazione provvisoria con gli stessi testi (non c'è ancora un firmware da seguire).
    function homeEmpty(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H;
      if (dev.cls === 'S') {
        figure(c, m, null, 4, 10, 4, 'idle', f);
        const x = 120, w = W - x - 6;
        T(c, clock(m), W - 6, 4, 8, { f: F_PIX, w: 400, a: 'right' });
        c.font = '400 8px ' + F_PIX;
        workS(c, m, alertsS(c, m, battS(c, m, W - 6 - Math.ceil(c.measureText(clock(m)).width) - 6, 4, INK) - 6, 4, true), 4, x);
        T(c, 'NESSUNA', x, 18, 20, { w: 700, max: w });
        T(c, 'sessione aperta', x, 44, 13, { w: 600, max: w });
        wrapT(c, 'Apri Claude Code sul PC: le sessioni compaiono qui.', x, 61, 10, w, 3, 12, { w: 500 });
        return;
      }
      const mt = MET(dev), sb = statusBar(c, dev, m, 'CLAUDE CODE', false, hit, ctl);
      const ms = Math.max(3, Math.floor((H - mt.hint - sb) * 0.45 / 25)), cx = Math.round(W / 2);
      let y = sb + mt.pad * 2;
      figure(c, m, null, cx - Math.round(AV_W * ms / 2), y, ms, 'idle', f);
      y += 25 * ms + mt.pad;
      T(c, 'Nessuna sessione aperta', cx, y, mt.f1, { w: 700, a: 'center', max: W - mt.pad * 4 });
      y += mt.f1 + mt.pad;
      wrapT(c, 'Apri Claude Code sul PC: le sessioni compaiono qui.', cx, y, mt.f2, W - mt.pad * 4, 2, mt.f2 + 4, { w: 600, a: 'center' });
      hints(c, dev, hintSet(dev, 'home'));
    }
    function homeS(c, dev, m, f, hit, ctl) {
      const st = m.set.homeStyle;
      if (st === 'righe') homeRighe(c, dev, m, f, hit, ctl);
      else if (st === 'contatori') homeNumeri(c, dev, m, f, hit, ctl);
      else if (st === 'una') homeUna(c, dev, m, f, hit, ctl);
      else homeGoccia(c, dev, m, f, hit, ctl);
    }
    function homeM(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, sb = statusBar(c, dev, m, 'CLAUDE CODE', false, hit, ctl);
      const foc = m.sessions[urgentIdx(m)], cx = 72;
      figure(c, m, foc, cx - Math.round(AV_W * 4 / 2), sb + 6, 4, pose(m, foc), f);
      T(c, STATES[foc.state].label, cx, sb + 110, 17, { w: 700, a: 'center', max: 140 });
      T(c, foc.name, cx, sb + 132, 13, { w: 600, a: 'center', max: 140 });
      wrapT(c, lineFor(foc), cx, sb + 152, 11, 136, 2, 14, { w: 600, a: 'center' });
      T(c, 'da ' + ago(foc.since), cx, sb + 184, 11, { w: 600, a: 'center' });
      const att = m.sessions.filter(isAttn).length;
      if (att) { c.fillStyle = INK; c.fillRect(cx - 60, sb + 206, 120, 20); T(c, att === 1 ? '1 attende te' : att + ' attendono te', cx, sb + 210, 11, { w: 700, a: 'center', c: PAPER }); }
      else T(c, 'nessuna attesa', cx, sb + 210, 11, { w: 600, a: 'center' });
      dotsV(c, 146, sb + 6, H - 24);
      const x0 = 166, w = W - x0 - 8, rh = 62;
      m.sessions.forEach((s, i) => {
        const y = sb + 6 + i * rh, a = isAttn(s), col = a ? PAPER : INK;
        if (i === m.ui.sel) box(c, x0 - 6, y - 3, w + 12, rh - 4, INK, 2);
        if (a) { c.fillStyle = INK; c.fillRect(x0 - 2, y, w + 4, 20); }
        icon(c, s.state, x0, y + 3, 2, f, col);
        T(c, s.name, x0 + 20, y + 2, 14, { w: 700, c: col, max: w - 100 });
        T(c, STATES[s.state].label, x0 + w, y + 5, 10, { w: 700, a: 'right', c: col });
        bar(c, x0, y + 25, w, 11, s, f);
        T(c, metaLine(s), x0, y + 40, 10, { w: 600, max: w });
        if (hit) hit(x0 - 6, y - 3, w + 12, rh - 4, () => ctl.openSession(i));
      });
      hints(c, dev, hintSet(dev, 'home'));
    }
    function homeW(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, mt = MET(dev), sb = statusBar(c, dev, m, 'CLAUDE CODE', false, hit, ctl);
      const top0 = sb + 8, bottom = H - mt.hint - 4, n = m.sessions.length, cw = W / n;
      m.sessions.forEach((s, i) => {
        const x = Math.round(i * cw), w = Math.round(cw), mx = x + w / 2, a = isAttn(s), col = a ? PAPER : INK;
        if (i === m.ui.sel) box(c, x + 3, top0 - 4, w - 6, bottom - top0 + 6, INK, 2);
        if (a) { c.fillStyle = INK; c.fillRect(x + 10, top0 + 2, w - 20, 22); } else box(c, x + 10, top0 + 2, w - 20, 22, INK, 1);
        T(c, s.name, mx, top0 + 6, 13, { w: 700, a: 'center', c: col, max: w - 32 });
        figure(c, m, s, Math.round(mx - AV_W * 3 / 2), top0 + 30, 3, pose(m, s), f + i * 3);
        T(c, STATES[s.state].label, mx, top0 + 110, 16, { w: 700, a: 'center', max: w - 20 });
        T(c, stateText(s), mx, top0 + 131, 11, { w: 600, a: 'center', max: w - 24 });
        bar(c, x + 16, top0 + 150, w - 32, 12, s, f);
        T(c, s.total ? s.done + '/' + s.total + ' task' : 'senza task', x + 16, top0 + 168, 11, { w: 600 });
        T(c, ago(s.since), x + w - 16, top0 + 168, 11, { w: 600, a: 'right' });
        const ex = extras(s);
        if (ex.length) T(c, ex.join(' · '), mx, top0 + 186, 11, { w: 700, a: 'center', max: w - 24 });
        if (i < n - 1) dotsV(c, Math.round(x + cw), top0 + 4, bottom - 4);
        if (hit) hit(x, top0, w, bottom - top0, () => ctl.openSession(i));
      });
      hints(c, dev, hintSet(dev, 'home'));
    }
    function homeL(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, mt = MET(dev), k = H / 480, g = dev.gray, sb = statusBar(c, dev, m, 'CLAUDE CODE', false, hit, ctl);
      const leftW = Math.round(W * 0.34), top0 = sb + Math.round(10 * k), logH = Math.round(96 * k), bottom = H - mt.hint - logH;
      const foc = m.sessions[urgentIdx(m)];
      if (g) { c.fillStyle = '#e4e4e4'; c.fillRect(Math.round(12 * k), top0, leftW - Math.round(24 * k), bottom - top0 - Math.round(8 * k)); }
      const ms = Math.max(4, Math.floor((bottom - top0) * 0.5 / 25)), cx = Math.round(leftW / 2);
      figure(c, m, foc, cx - Math.round(AV_W * ms / 2), top0 + Math.round(8 * k), ms, pose(m, foc), f);
      let y = top0 + Math.round(8 * k) + 25 * ms + Math.round(6 * k);
      T(c, STATES[foc.state].label, cx, y, mt.f1, { w: 700, a: 'center', max: leftW - 40 }); y += mt.f1 + Math.round(8 * k);
      T(c, foc.name, cx, y, mt.f2, { w: 600, a: 'center', max: leftW - 40 }); y += mt.f2 + Math.round(6 * k);
      wrapT(c, lineFor(foc), cx, y, mt.f3, leftW - 48, 2, mt.f3 + 4, { w: 500, a: 'center' });
      dotsV(c, leftW, top0, bottom - 4);
      const x0 = leftW + Math.round(16 * k), w = W - x0 - Math.round(16 * k), n = m.sessions.length, rh = Math.floor((bottom - top0) / n);
      m.sessions.forEach((s, i) => {
        const yy = top0 + i * rh, a = isAttn(s), col = a ? PAPER : INK, bandH = mt.f2 + Math.round(10 * k), is = Math.max(2, Math.round(2 * k));
        if (i === m.ui.sel) { if (g) { c.fillStyle = '#d6d6d6'; c.fillRect(x0 - 8, yy - 3, w + 16, rh - 4); } else box(c, x0 - 8, yy - 3, w + 16, rh - 4, INK, 2); }
        if (a) { c.fillStyle = INK; c.fillRect(x0 - 4, yy, w + 8, bandH); }
        icon(c, s.state, x0, yy + Math.round((bandH - 7 * is) / 2), is, f, col);
        T(c, s.name, x0 + 7 * is + 10, yy + Math.round(4 * k), mt.f2, { w: 700, c: col, max: Math.round(w * 0.55) });
        T(c, STATES[s.state].label, x0 + w, yy + Math.round(6 * k), mt.f3, { w: 700, a: 'right', c: col });
        bar(c, x0, yy + bandH + Math.round(6 * k), w, Math.round(13 * k), s, f, g);
        T(c, metaLine(s), x0, yy + bandH + Math.round(24 * k), mt.f3, { w: 600, max: w });
        if (hit) hit(x0 - 8, yy - 3, w + 16, rh - 4, () => ctl.openSession(i));
      });
      const ly = bottom;
      c.fillStyle = INK; c.fillRect(0, ly, W, 2);
      T(c, 'ULTIMI EVENTI', mt.pad, ly + Math.round(8 * k), mt.f3 - 2, { w: 700 });
      const nxt = m.agenda.find((a) => a.t > now());
      if (nxt) T(c, 'PROSSIMO  ' + hm(new Date(nxt.t)) + '  ' + nxt.title, W - mt.pad, ly + Math.round(8 * k), mt.f3 - 2, { w: 700, a: 'right', max: Math.round(W / 2) });
      const lh = mt.f3 + Math.round(5 * k);
      m.log.slice(0, Math.max(1, Math.floor((logH - mt.f3 - Math.round(16 * k)) / lh))).forEach((e, q) => {
        T(c, hm(e.t) + '    ' + e.name + '    ' + e.text, mt.pad, ly + Math.round(10 * k) + mt.f3 + q * lh, mt.f3, { w: 600, max: W - mt.pad * 2 });
      });
      hints(c, dev, hintSet(dev, 'home'));
    }

    /* ---------- Sessione ---------- */
    function detailS(c, dev, m, f, hit, ctl) {
      const W = dev.W, i = m.ui.sel, s = m.sessions[i], a = isAttn(s);
      c.fillStyle = INK; c.fillRect(0, 0, W, 15);
      chevL(c, 4, 3, PAPER, 1);
      const pos = (i + 1) + '/' + m.sessions.length;
      c.font = '400 8px ' + F_PIX;
      const bx = battS(c, m, W - 4 - Math.ceil(c.measureText(pos).width) - 6, 4, PAPER);
      T(c, s.name, 13, 1, 12, { w: 700, c: PAPER, max: Math.min(170, bx - 13 - 6) });
      T(c, pos, W - 4, 4, 8, { f: F_PIX, w: 400, a: 'right', c: PAPER });
      if (hit) hit(0, 0, 40, 15, ctl.back);
      figure(c, m, s, 2, 20, 3, pose(m, s), f);
      const x = 88, w = W - x - 4;
      if (a) { c.fillStyle = INK; c.fillRect(x - 2, 18, w + 4, 20); }
      T(c, STATES[s.state].label, x, 19, 16, { w: 700, c: a ? PAPER : INK, max: w });
      wrapT(c, s.detail, x, 41, 10, w, 2, 12, { w: 500 });
      bar(c, x, 68, w, 8, s, f);
      T(c, (s.total ? s.done + '/' + s.total + ' task' : 'senza task') + ' · ' + ago(s.since) + ' · ctx ' + s.ctx + '%', x, 79, 10, { w: 600, max: w });
      const cur = s.tasks.find((t) => t[1] === 1) || s.tasks.find((t) => t[1] === 0);
      if (s.state === 'done' && s.summary) T(c, s.summary, x, 92, 10, { w: 600, max: w });
      else if (cur) T(c, '› ' + cur[0], x, 92, 10, { w: 600, max: w });
      dotsH(c, 0, W, 106);
      let line = '1X SESSIONE   2X APRI   LUNGA INDIETRO';
      if (s.state === 'permission') line = permFromDisplay(m) ? '2X  RISPONDI AL PERMESSO' : 'RISPONDI NEL TERMINALE';
      else if (s.state === 'input') line = 'RISPONDI NEL TERMINALE';
      else if (canArchive(s)) line = '1X SESSIONE   2X ARCHIVIA   LUNGA INDIETRO';
      else line = '1X SESSIONE SUCCESSIVA   LUNGA INDIETRO';
      T(c, line, 4, 110, 8, { f: F_PIX, w: s.state === 'permission' ? 700 : 400, max: W - 8 });
      if (hit && s.state === 'permission') hit(0, 106, W, 16, ctl.ok);
    }
    function okLabel(dev) { return dev.input === 'single' ? (dev.cls === 'S' ? '2×' : '2 pressioni') : dev.input === 'wheel' ? 'Premi la rotella' : 'Premi OK'; }
    function detailBig(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, mt = MET(dev), i = m.ui.sel, s = m.sessions[i], g = dev.gray;
      const sb = statusBar(c, dev, m, s.name + '  ·  ' + (i + 1) + '/' + m.sessions.length, true, hit, ctl);
      const pad = mt.pad, top0 = sb + pad, bottom = H - mt.hint - pad;
      const ms = Math.max(3, Math.min(Math.floor((bottom - top0) * 0.55 / 25), Math.floor(W * 0.28 / 27)));
      const leftW = 27 * ms + pad * 2, cx = Math.round(leftW / 2);
      figure(c, m, s, pad, top0, ms, pose(m, s), f);
      let y = top0 + 25 * ms + Math.round(pad * 0.6);
      T(c, STATES[s.state].label, cx, y, mt.f1, { w: 700, a: 'center', max: leftW - 8 }); y += mt.f1 + 4;
      T(c, 'da ' + ago(s.since), cx, y, mt.f3, { w: 600, a: 'center' }); y += mt.f3 + 4;
      T(c, s.total ? s.done + '/' + s.total + ' task' : 'senza task', cx, y, mt.f3, { w: 600, a: 'center' }); y += mt.f3 + 6;
      bar(c, pad, y, leftW - pad * 2, Math.max(9, mt.f3), s, f, g);
      y += Math.max(9, mt.f3) + 6;
      const putL = (txt, bold) => { if (y + mt.f3 > bottom) return; T(c, txt, cx, y, mt.f3, { w: bold ? 700 : 600, a: 'center', max: leftW - 8 }); y += mt.f3 + 4; };
      putL('contesto ' + s.ctx + '%', s.ctx >= 80);
      if (s.subs) putL('+' + s.subs + (s.subs === 1 ? ' subagente' : ' subagenti'), false);
      if (s.test) putL(s.test === 'ok' ? 'test superati' : 'test falliti', s.test !== 'ok');
      dotsV(c, leftW + 4, top0, bottom);
      const x0 = leftW + pad * 2, w = W - x0 - pad, twoCol = dev.cls !== 'M', colW = twoCol ? Math.round((w - pad * 2) / 2) : w;
      let yy = top0;
      T(c, 'ORA', x0, yy, mt.f3 - 1, { w: 700 }); yy += mt.f3 + 2;
      wrapT(c, s.detail, x0, yy, mt.f2, colW, 2, mt.f2 + 3, { w: 600 }); yy += (mt.f2 + 3) * 2 + 4;
      dotsH(c, x0, x0 + colW, yy); yy += 6;
      T(c, 'TASK', x0, yy, mt.f3 - 1, { w: 700 });
      if (s.total) T(c, s.done + ' di ' + s.total, x0 + colW, yy, mt.f3 - 1, { w: 600, a: 'right' });
      yy += mt.f3 + 4;
      const lh = mt.f3 + Math.round(mt.f3 * 0.6), bs = Math.round(mt.f3 * 0.9);
      const limit = twoCol ? bottom : bottom - (mt.f2 * 2 + 30);
      const maxRows = Math.max(1, Math.floor((limit - yy) / lh));
      const cur = Math.max(0, s.tasks.findIndex((t) => t[1] !== 2)), start = Math.max(0, Math.min(cur - 2, s.tasks.length - maxRows));
      s.tasks.slice(start, start + maxRows).forEach((t, q) => {
        const ty = yy + q * lh;
        checkbox(c, x0, ty + 1, t[1], f, bs);
        T(c, t[0], x0 + bs + 6, ty, mt.f3, { w: t[1] === 1 ? 700 : 500, max: colW - bs - 6 });
      });
      const bx = twoCol ? x0 + colW + pad * 2 : x0, bw = twoCol ? colW : w;
      let by = twoCol ? top0 : bottom - (mt.f2 * 2 + 22);
      const pending = s.state === 'permission' || s.state === 'input';
      if (pending) {
        const bh = mt.f2 * 2 + 22;
        c.fillStyle = INK; c.fillRect(bx - 4, by, bw + 8, bh);
        T(c, s.state === 'permission' ? 'CHIEDE UN PERMESSO' : 'ASPETTA UNA RISPOSTA', bx + 6, by + 6, mt.f2 - 2, { w: 700, c: PAPER, max: bw - 12 });
        T(c, s.state === 'permission' && permFromDisplay(m) ? okLabel(dev) + ' per rispondere' : 'Rispondi nel terminale', bx + 6, by + 10 + mt.f2, mt.f3, { w: 600, c: PAPER, max: bw - 12 });
        if (hit && s.state === 'permission') hit(bx - 4, by, bw + 8, bh, ctl.ok);
        by += bh + pad;
      }
      if (twoCol && s.summary && s.state === 'done') {
        T(c, 'ULTIMO MESSAGGIO', bx, by, mt.f3 - 1, { w: 700 }); by += mt.f3 + 4;
        const ls = wrapLines(c, s.summary, mt.f2, bw, 600).slice(0, 3);
        ls.forEach((ln, q) => T(c, ln, bx, by + q * (mt.f2 + 3), mt.f2, { w: 600, max: bw }));
        by += ls.length * (mt.f2 + 3) + pad;
      }
      if (twoCol) {
        T(c, 'ULTIMI EVENTI', bx, by, mt.f3 - 1, { w: 700 }); by += mt.f3 + 4;
        const eh = mt.f3 + 6;
        m.log.filter((e) => e.name === s.name).slice(0, Math.max(0, Math.floor((bottom - by) / eh))).forEach((e, q) => T(c, hm(e.t) + '   ' + e.text, bx, by + q * eh, mt.f3, { w: 500, max: bw }));
      } else if (!pending && s.state === 'done' && s.summary) {
        T(c, 'ULTIMO MESSAGGIO', bx, by + 4, mt.f3 - 1, { w: 700 });
        wrapT(c, s.summary, bx, by + 8 + mt.f3, mt.f3, bw, 2, mt.f3 + 3, { w: 600 });
      } else if (!pending) {
        const last = m.log.find((e) => e.name === s.name);
        if (last) { T(c, 'ULTIMO EVENTO', bx, by + 4, mt.f3 - 1, { w: 700 }); T(c, hm(last.t) + '   ' + last.text, bx, by + 8 + mt.f3, mt.f3, { w: 600, max: bw }); }
      }
      hints(c, dev, hintSet(dev, 'detail'));
    }

    /* ---------- Permesso ---------- */
    function splitCmd(d) { const p = String(d).split(': '); return { tool: p[0], arg: p.slice(1).join(': ') }; }
    function permS(c, dev, m, f, hit, ctl) {
      const W = dev.W, tp = top(m), s = m.sessions[tp.i], cmd = splitCmd(s.detail);
      c.fillStyle = INK; c.fillRect(0, 0, W, 15);
      T(c, 'PERMESSO', 4, 1, 12, { w: 700, c: PAPER });
      T(c, s.name, W - 4, 2, 11, { w: 600, c: PAPER, a: 'right', max: 140 });
      box(c, 4, 19, W - 8, 34, INK, 1);
      T(c, cmd.tool, 9, 22, 12, { w: 700, max: W - 18 });
      T(c, cmd.arg || '—', 9, 37, 11, { w: 600, max: W - 18 });
      const bw = Math.floor((W - 14) / 2);
      ['CONSENTI', 'NEGA'].forEach((lab, k) => {
        const x = 4 + k * (bw + 6), on = tp.choice === k;
        if (on) { c.fillStyle = INK; c.fillRect(x, 58, bw, 26); } else box(c, x, 58, bw, 26, INK, 2);
        T(c, lab, x + bw / 2, 63, 14, { w: 700, a: 'center', c: on ? PAPER : INK });
        if (hit) hit(x, 58, bw, 26, () => ctl.permTap(k));
      });
      T(c, permWaitText(permSince(m, tp)), 4, 89, 8, { f: F_PIX, w: 700, max: W - 8 });
      dotsH(c, 0, W, 104);
      T(c, '1X CAMBIA   2X CONFERMA   LUNGA ANNULLA', 4, 109, 8, { f: F_PIX, w: 400, max: W - 8 });
    }
    function permBig(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, mt = MET(dev), tp = top(m), s = m.sessions[tp.i], cmd = splitCmd(s.detail);
      const sb = statusBar(c, dev, m, 'PERMESSO RICHIESTO', false, hit, ctl);
      const pad = mt.pad, y0 = sb + pad, bottom = H - mt.hint - pad;
      const ms = Math.max(3, Math.floor((bottom - y0) * 0.38 / 25));
      figure(c, m, s, pad, y0, ms, 'permission', f);
      const x0 = pad * 2 + 27 * ms, w = W - x0 - pad;
      T(c, s.name + ' vuole usare:', x0, y0, mt.f2, { w: 600, max: w });
      const bh = mt.f1 + mt.f2 * 2 + 16, by0 = y0 + mt.f2 + 8;
      box(c, x0, by0, w, bh, INK, 2);
      T(c, cmd.tool, x0 + 10, by0 + 6, mt.f1, { w: 700, max: w - 20 });
      wrapT(c, cmd.arg || '—', x0 + 10, by0 + 10 + mt.f1, mt.f2, w - 20, 2, mt.f2 + 3, { w: 600 });
      const byy = Math.max(y0 + 25 * ms + pad, by0 + bh + pad);
      const bH = Math.round(mt.f1 * 2.2), bw2 = Math.floor((W - pad * 3) / 2);
      ['CONSENTI', 'NEGA'].forEach((lab, k) => {
        const x = pad + k * (bw2 + pad), on = tp.choice === k;
        if (on) { c.fillStyle = INK; c.fillRect(x, byy, bw2, bH); } else box(c, x, byy, bw2, bH, INK, 2);
        T(c, lab, x + bw2 / 2, byy + Math.round((bH - mt.f1) / 2), mt.f1, { w: 700, a: 'center', c: on ? PAPER : INK });
        if (hit) hit(x, byy, bw2, bH, () => ctl.permTap(k));
      });
      let yy = byy + bH + pad;
      wrapT(c, 'Puoi rispondere qui o nel terminale: vale la prima risposta. La richiesta non scade.', W / 2, yy, mt.f3, W - pad * 6, 2, mt.f3 + 3, { w: 600, a: 'center' });
      yy += (mt.f3 + 3) * 2 + 4;
      T(c, permWaitText(permSince(m, tp)), W / 2, yy, mt.f3, { w: 700, a: 'center' });
      hints(c, dev, hintSet(dev, 'perm'));
    }

    /* ---------- Abbinamento e conferme (M5) ---------- */
    // Classe S (la Heltec). Sulle altre classi lo stesso disegno in alto a sinistra finché M7 non le ridisegna.
    function pairS(c, dev, m, f, hit, ctl) {
      const W = dev.W, tp = top(m), ready = tp.sel === 1;
      c.fillStyle = INK; c.fillRect(0, 0, W, 15);
      T(c, 'ABBINAMENTO', 4, 1, 12, { w: 700, c: PAPER });
      T(c, 'calamaio.local', W - 4, 2, 11, { w: 600, c: PAPER, a: 'right', max: 130 });
      T(c, ready ? 'Il PC ha il codice giusto' : 'Scrivi questo codice sul PC', W / 2, 21, 10, { w: 600, a: 'center', max: W - 8 });
      const code = tp.code.slice(0, 3) + ' ' + tp.code.slice(3);  // «175 397»: si legge e si ricopia meglio
      T(c, code, W / 2, 33, 44, { w: 700, a: 'center' });
      if (ready) {  // il PC ha confermato il codice: ora tocca al display
        c.font = '700 44px ' + F_UI;
        const tw = Math.ceil(c.measureText(code).width);
        box(c, Math.round(W / 2 - tw / 2) - 10, 35, tw + 20, 40, INK, 2);
      }
      T(c, ready ? 'Conferma qui con 2 pressioni' : 'Poi conferma qui con 2 pressioni', W / 2, 86, 10, { w: ready ? 700 : 500, a: 'center', max: W - 8 });
      dotsH(c, 0, W, 104);
      T(c, '2X CONFERMA   LUNGA ANNULLA', 4, 109, 8, { f: F_PIX, w: 400, max: W - 8 });
      if (hit) hit(0, 15, W, 89, ctl.ok);
    }
    function confirmS(c, dev, m, f, hit, ctl) {
      const W = dev.W, tp = top(m), protect = tp.kind === 'protect', armed = tp.until > now();
      const t0 = viewHeader(c, dev, m, protect ? 'Proteggi le chiavi' : 'Ripristino di fabbrica', hit, ctl);
      wrapT(c, protect
        ? 'Scrive nel chip una chiave segreta che non si può più cancellare né leggere. Da lì in poi password del Wi-Fi e abbinamento restano cifrati e valgono solo su questa scheda.'
        : 'Cancella rete Wi-Fi, abbinamento, impostazioni e sale delle chiavi, poi riavvia. Il display andrà configurato di nuovo dal telefono.',
        6, t0 + 3, 10, W - 12, 5, 12, { w: 500 });
      if (armed) { c.fillStyle = INK; c.fillRect(4, 84, W - 8, 16); T(c, 'TIENI PREMUTO 3 SECONDI', W / 2, 87, 10, { w: 700, a: 'center', c: PAPER }); }
      else { box(c, 4, 84, W - 8, 16, INK, 1); T(c, '2X PER CONTINUARE', W / 2, 87, 10, { w: 700, a: 'center' }); }
      dotsH(c, 0, W, 104);
      T(c, armed ? 'TIENI 3 S CONFERMA   3X ANNULLA' : '2X CONTINUA   LUNGA ANNULLA', 4, 109, 8, { f: F_PIX, w: 400, max: W - 8 });
    }

    /* ---------- Menu ---------- */
    // arg: SSID della rete per 'savednet' (tp.ssid della lista aperta).
    function menuList(id, dev, m, arg) {
      const S = dev.cls === 'S', w = m.sys;
      if (id === 'wifi') return { title: 'Wi-Fi', items: [
        { label: 'Rete', val: w.wifi ? w.ssid : 'nessuna', help: w.wifi ? 'Il display è collegato a ' + w.ssid + '.' : 'Nessuna rete salvata: usa Cerca reti o Configura da telefono.' },
        { label: 'Segnale', bars: w.wifi ? rssiBars(w.rssi) : -1, val: w.wifi ? w.rssi + ' dBm' : '', help: 'Sotto −75 dBm gli aggiornamenti possono arrivare in ritardo.' },
        { label: 'Indirizzo', val: w.wifi ? w.ip : '—', help: 'Indirizzo assegnato dal router.' },
        { label: 'Reti salvate', val: m.saved.length + ' di ' + SAVED_MAX, go: 'saved', help: 'Fino a 5 reti. Il display usa la più forte tra quelle che vede e, se il segnale cala sotto −80 dBm, passa da solo a una più forte.' },
        { label: 'Cerca reti', open: 'wifiScan', help: 'Elenca le reti vicine con il segnale. Le reti protette si configurano dal telefono.' },
        { label: 'Configura da telefono', open: 'qr', help: 'Il display crea la rete Calamaio-3F2A e mostra un QR: dal telefono scegli la rete e scrivi la password. La rete si aggiunge a quelle salvate.' },
        { label: 'Server PC', val: w.pc ? 'calamaio.local' : 'non risponde', help: 'Il servizio Calamaio sul PC, trovato in automatico in rete locale.' },
        { label: 'Hook diretti', val: yes(m.set.hookDirect), act: 'hook', help: 'Claude Code manda gli eventi direttamente a http://calamaio-3f2a.local/hook, anche da un server o da un secondo PC. I permessi passano sempre dal servizio sul PC.' },
        { label: 'Dimentica tutte le reti', val: m.ui.forgetUntil > now() ? 'conferma?' : undefined, act: 'forget', help: 'Cancella tutte le reti salvate e le password, poi riavvia. Al riavvio parte la configurazione.' }
      ] };
      // Reti salvate: SSID, «in uso», segnale se la rete si vede (NETS è la ricerca finta; la rete in uso ha il segnale vero).
      const savedRssi = (s) => (w.wifi && w.ssid === s.ssid ? w.rssi : (NETS.find((n) => n.ssid === s.ssid) || {}).rssi);
      if (id === 'saved') {
        const items = m.saved.map((s) => {
          const cur = w.wifi && w.ssid === s.ssid, r = savedRssi(s);
          return { label: s.ssid, val: cur ? 'in uso' : undefined, bars: r !== undefined ? rssiBars(r) : undefined, go: 'savednet', ssid: s.ssid,
            help: cur ? 'Il display è collegato a questa rete.' : r !== undefined ? 'Si vede da qui: segnale ' + r + ' dBm.' : 'Non si vede da qui.' };
        });
        return { title: 'Reti salvate', items: items.length ? items : [{ label: 'Nessuna rete', help: 'Aggiungi una rete con Configura da telefono o dalla webapp.' }] };
      }
      if (id === 'savednet') {
        const s = m.saved.find((x) => x.ssid === arg) || { ssid: arg || '', used: 0 }, cur = w.wifi && w.ssid === s.ssid, r = savedRssi(s);
        const d = new Date(s.used);
        return { title: s.ssid, items: [
          { label: 'Segnale', bars: r !== undefined ? rssiBars(r) : -1, val: r !== undefined ? r + ' dBm' : 'non visibile', help: 'Segnale della rete adesso, se si vede.' },
          { label: 'Ultimo uso', val: cur ? 'in uso' : s.used ? pad2(d.getDate()) + '/' + pad2(d.getMonth() + 1) : 'mai', help: 'Ultima volta che il display si è collegato a questa rete.' },
          { label: 'Dimentica', val: m.ui.forgetNetUntil > now() ? 'conferma?' : undefined, act: 'forgetnet', help: 'Cancella questa rete e la sua password, poi riavvia. Le altre reti salvate restano.' }
        ] };
      }
      if (id === 'display') return { title: 'Display', items: [].concat(
        S ? [{ label: 'Stile home', val: HOME_LABEL[m.set.homeStyle], act: 'style', help: 'Goccia, Righe, Contatori o Una alla volta.' }] : [],
        [
          { label: 'Animazioni', val: ANIM_LABEL[m.set.anim], act: 'anim', help: 'Auto muove la mascotte a raffiche e quando qualcuno ti aspetta. Su questo pannello le animazioni sono ' + profileName(dev) + '.' },
          { label: 'Velocità animazioni', val: SPEED_LABEL[m.set.speed], act: 'speed', help: 'Profilo usa la velocità prevista per questo pannello. Massima aggiorna appena il pannello è pronto e usa tutti e 4 i fotogrammi: più fluida, ma consuma di più e lascia più scie.' },
          { label: 'Risparmio energetico', val: yes(m.set.save), act: 'save', help: 'Ferma le animazioni e aggiorna lo schermo solo quando cambia qualcosa. Si attiva da solo sotto il 15% di batteria.' },
          { label: 'Inverti colori', val: yes(m.set.invert), act: 'invert', help: 'Testo chiaro su fondo nero.' },
          { label: 'Ruota 180°', val: yes(m.set.rot), act: 'rot', help: 'Per montarlo capovolto, con il cavo dall’altra parte.' },
          { label: 'Refresh completo', val: 'esegui', act: 'full', help: 'Pulisce le scie degli aggiornamenti parziali. Parte da solo ogni ' + dev.fullEvery + ' aggiornamenti.' },
          { label: 'Profilo pannello', val: dev.fps + '/s · ' + dev.fullEvery, help: 'Velocità stimata del refresh parziale e ogni quanti aggiornamenti scatta quello completo.' },
          { label: 'Gioco della mascotte', val: yes(m.set.game), act: 'game', help: 'Livelli, traguardi e accessori da indossare, guadagnati lavorando con Claude Code. Spento: niente accessori né feste; i progressi restano sul PC.' },
          { label: 'Tema', val: 'Calamaio', go: 'tema', help: 'Tema delle schermate. Calamaio è quello di serie; gli altri arrivano dalla webapp.' }
        ]) };
      // Gioco della mascotte: scelta dell'accessorio tra gli sbloccati (o nessuno). 2× indossa e lo dice al PC (accessory-wear).
      if (id === 'petacc') {
        const wid = m.pet.wear ? m.pet.wear.id : '';
        return { title: 'Accessorio', items: [{ label: 'Nessuno', val: wid ? undefined : 'indossato', act: 'wear', wear: '', help: 'La mascotte senza accessori.' }].concat(
          m.pet.unlocked.map((u) => ({ label: u.name, val: u.id === wid ? 'indossato' : undefined, act: 'wear', wear: u.id,
            help: 'Sbloccato con il gioco. Lo indossano tutte le mascotte che hanno i punti di aggancio.' }))) };
      }
      // Temi delle schermate (firmware 0.13.0): nel simulatore c'è solo quello di serie.
      if (id === 'tema') return { title: 'Tema', items: [{ label: 'Calamaio', val: '✓', act: 'theme', help: 'Il tema di serie: sempre presente.' }] };
      if (id === 'avvisi') return { title: 'Avvisi', items: [].concat([
        { label: 'Silenzia', val: !muted(m) ? 'no' : (m.set.muteKind === 'night' ? 'fino alle 8' : '1 ora'), act: 'mute', help: 'Ferma i richiami animati e il suono. Le sessioni restano visibili.' },
        { label: 'Notturno 22-8', val: yes(m.set.night), act: 'night', help: 'Di notte niente animazioni e un aggiornamento ogni 5 minuti. Qui lo attivi subito per provarlo.' },
        { label: 'Permessi dal display', val: yes(m.set.permOn), act: 'perm', help: 'Permette di rispondere Consenti o Nega dal display. NEGA è sempre preselezionato.' }
      ], (dev.buzzer || m.acc) ? [{ label: dev.buzzer ? 'Cicalino' : 'Buzzer esterno', val: yes(m.set.sound), act: 'sound', help: 'Un bip quando una sessione chiede un permesso.' }] : [],
      dev.led ? [{ label: 'LED', val: yes(m.set.led), act: 'led', help: 'Il LED lampeggia finché qualcuno ti aspetta.' }] : []) };
      // M6: Menu › Batteria è un sottomenu: livello (la vista di prima), calibrazione, nuova batteria.
      if (id === 'battery') return { title: 'Batteria', items: [
        { label: 'Livello', val: Math.round(w.batt) + '%', open: 'batteria', help: 'Livello, ricarica, autonomia stimata e modalità risparmio.' },
        { label: 'Calibra la batteria', val: m.bat.cal ? calDate(m) : 'mai', act: 'calib', help: 'Due ore a batteria, partendo almeno dal 90% e senza cavo: misura i consumi veri. Da rifare ogni volta che cambi la batteria. Stima ±30%.' },
        { label: 'Nuova batteria', val: m.ui.newBattUntil > now() ? 'conferma?' : undefined, act: 'newbatt', help: 'Cancella la calibrazione. Da rifare ogni volta che cambi la batteria.' }
      ] };
      if (id === 'security') return { title: 'Sicurezza', items: [
        { label: 'Abbinamento', val: pairLabel(m), act: 'pairinfo', help: 'Solo il PC abbinato può far rispondere ai permessi da qui. Per abbinare: sul PC esegui calamaio pair, oppure Abbina nella pagina Dispositivo.' },
        { label: 'Dimentica abbinamento', val: m.ui.unpairUntil > now() ? 'conferma?' : undefined, act: 'unpair', help: 'Cancella la chiave dell’abbinamento. I permessi tornano a rispondersi solo nel terminale.' },
        { label: 'Proteggi le chiavi', val: m.sys.keys ? 'protette' : 'non protette', act: 'protect', help: 'Scrive una chiave segreta nel chip, una volta sola e per sempre, e cifra con quella la password del Wi-Fi e l’abbinamento.' },
        { label: 'Ripristino di fabbrica', act: 'factory', help: 'Cancella Wi-Fi, abbinamento, impostazioni e sale delle chiavi, poi riavvia. La chiave nel chip resta.' }
      ] };
      if (id === 'info') return { title: 'Info', items: [
        { label: 'Sistema', val: 'Calamaio OS 0.2', help: 'Versione del firmware.' },
        { label: 'Accessori', val: m.acc ? 'CO2 e buzzer' : 'nessuno', help: 'Sensore e buzzer collegati ai pin di espansione.' },
        { label: 'Dispositivo', val: dev.name, help: dev.name + ' ' + dev.size.replace('″', '"') + '.' },
        { label: 'Schermo', val: dev.W + '×' + dev.H + (dev.gray ? ' · 16 grigi' : ' · b/n'), help: 'Risoluzione e colori del pannello.' },
        { label: 'Refresh', val: dev.fps + ' agg./s · ' + dev.fullEvery, help: 'Velocità stimata e ogni quanti aggiornamenti parte il refresh completo.' },
        { label: 'Comandi', val: ({ single: '1 tasto', wheel: 'rotella + 2 tasti', three: '3 tasti' })[dev.input] + (dev.touch ? ' + tocco' : ''), help: 'Tasti disponibili su questo dispositivo.' },
        { label: 'Indirizzo', val: w.wifi ? w.ip : '—', help: 'Indirizzo in rete locale.' },
        { label: 'Acceso da', val: ago(w.bootAt), help: 'Tempo dall’ultimo avvio.' },
        // M6: diagnosi del rilevamento del cavo (a batteria non c'è la seriale): campioni alti della linea dal convertitore USB.
        { label: 'Cavo USB', val: w.usb ? 'sì (8/8)' : 'no (0/8)', help: 'Letto dalla linea del convertitore USB-seriale ogni 10 secondi: campioni alti su 8. Il fulmine vuol dire cavo collegato, non carica in corso.' },
        { label: 'Riavvia', act: 'reboot', help: 'Riavvia il sistema e rifà la connessione.' }
      ] };
      if (id === 'tools') {
        const down = m.services.filter((x) => !x.ok).length, nx = m.agenda.find((a) => a.t > now());
        return { title: 'Strumenti', items: [
          { label: 'Concentrazione', val: focusOn(m) ? 'fino alle ' + hm(new Date(m.focus.until)) : m.focus.len + ' min', open: 'focus', help: 'Timer da 25 minuti: Goccia lavora con te e gli avvisi restano in silenzio finché non finisce.' },
          { label: 'Agenda', val: nx ? hm(new Date(nx.t)) : '—', open: 'agenda', help: 'Prossimi appuntamenti da un calendario ICS, letti dal servizio sul PC.' },
          { label: 'Servizi e PR', val: down ? down + ' non risponde' : 'tutto ok', open: 'servizi', help: 'Siti e servizi controllati dal PC, più lo stato delle pull request letto con gh.' },
          { label: 'Wi-Fi ospiti', open: 'guest', help: 'QR della rete ospiti: chi lo inquadra si collega senza scrivere la password.' },
          { label: 'Ambiente', val: m.acc ? Math.round(m.air.co2) + ' ppm' : (dev.sensor ? fmt1(m.sys.temp) + '°' : 'nessun sensore'), open: 'ambiente', help: 'CO2, temperatura e umidità da un sensore sui pin I2C.' },
          { label: 'Giornata di lavoro', val: m.work.on ? 'fino alle ' + hm(new Date(m.work.endAt)) : 'spenta', go: 'work', help: 'Quanto manca alla fine della giornata, nel Riposo e accanto all’orologio della Home.' }
        ] };
      }
      // Menu › Gioco (firmware 0.11.0): «Alto o basso», spostato da Strumenti. Controller e ROM solo sulla scheda vera.
      if (id === 'gb') {
        return { title: 'Gioco', items: [
          { label: 'Alto o basso', val: 'record ' + m.game.best, open: 'game', help: 'Indovina se la prossima carta sarà più alta o più bassa: 1× più alta, 2× più bassa.' }
        ] };
      }
      // Giornata di lavoro (0.9.0): la fine è fissa all'orario di fine (regola A). Inizio e Fine si cambiano dalla webapp.
      if (id === 'work') {
        const w = m.work;
        return { title: 'Giornata di lavoro', items: [
          { label: 'Inizia ora', val: w.on ? 'attiva' : undefined, act: 'workstart', help: 'Conto alla rovescia fino alle ' + minHm(w.end) + ': se inizi più tardi la giornata è più corta.' },
          { label: 'Ferma', act: 'workstop', help: 'Spegne il conto alla rovescia. Si spegne da solo alla fine e a mezzanotte.' },
          { label: 'Inizio', val: minHm(w.start), help: 'Inizio della giornata: la barra del Riposo parte da qui. Si cambia dalla webapp.' },
          { label: 'Fine', val: minHm(w.end), help: 'Fine della giornata. Si cambia dalla webapp.' }
        ] };
      }
      return { title: 'Menu', items: [].concat([
        { label: 'Limiti', val: m.limits.h5 != null ? '5h ' + Math.round(m.limits.h5) + '%' : '5h —', open: 'limiti', help: 'Uso delle finestre da 5 ore e da 7 giorni (piani Pro e Max) e contesto di ogni sessione, dalla status line di Claude Code.' },
        { label: 'Strumenti', go: 'tools', help: 'Concentrazione, agenda, servizi e PR, Wi-Fi ospiti, ambiente, giornata di lavoro.' }
      ], m.set.game ? [{ label: 'La mascotte', val: m.pet.known ? 'livello ' + m.pet.level : '', open: 'pet', help: 'Livello, energia, ultimi traguardi e accessorio della mascotte.' }] : [],
      [{ label: 'Gioco', go: 'gb', help: 'Alto o basso e, col controller Bluetooth, i giochi del Game Boy.' }], [
        { label: 'Wi-Fi', val: w.wifi ? w.ssid : 'non connesso', go: 'wifi', help: 'Rete, segnale, configurazione dal telefono e collegamento al PC.' },
        { label: 'Display', go: 'display', help: 'Stile della home, animazioni, colori invertiti, rotazione e refresh.' },
        { label: 'Avvisi', val: muted(m) ? 'silenziati' : (m.set.night ? 'notturno' : ''), go: 'avvisi', help: 'Silenzia, notturno, risposte ai permessi' + (dev.buzzer ? ', cicalino e LED.' : '.') },
        { label: 'Sicurezza', val: pairShort(m), go: 'security', help: 'Abbinamento con il PC, chiavi nel chip e ripristino di fabbrica.' },
        { label: 'Oggi', open: 'oggi', help: 'Task completati, permessi e tempo medio di attesa della giornata.' },
        { label: 'Storico', open: 'storico', help: dev.sd ? 'Tutti gli eventi, salvati sulla scheda SD, un file al giorno.' : 'Gli ultimi 80 eventi, tenuti in memoria.' },
        { label: 'Batteria', val: Math.round(w.batt) + '%', go: 'battery', help: 'Livello, ricarica, autonomia e calibrazione.' }
      ], S ? [{ label: 'Stato', open: 'stato', help: 'Wi-Fi, batteria e PC in grande.' }] : [], [
        { label: 'Riposo', act: 'sleep', help: 'Orologio grande e mascotte che dorme; un tasto la sveglia.' },
        { label: 'Info', go: 'info', help: 'Versione, schermo, profilo di refresh e indirizzo.' }
      ]) };
    }
    function netItems(m) {
      return NETS.map((n) => ({ label: n.ssid + (m.sys.wifi && m.sys.ssid === n.ssid ? '  ✓' : ''), bars: rssiBars(n.rssi), lock: n.lock, net: n, help: (n.lock ? 'Rete protetta: la password si inserisce dal telefono.' : 'Rete aperta: si collega subito.') + ' Segnale ' + n.rssi + ' dBm.' }));
    }
    function listScreen(c, dev, m, f, hit, ctl, title, items, sel) {
      const W = dev.W, H = dev.H, mt = MET(dev);
      if (dev.cls === 'S') {
        stripS(c, dev, m, title, hit, ctl);
        const rh = 18, y0 = 16, vis = Math.floor((H - y0) / rh);
        const off = clamp(sel - Math.floor(vis / 2), 0, Math.max(0, items.length - vis));
        items.slice(off, off + vis).forEach((it, q) => {
          const i = off + q, y = y0 + q * rh, on = i === sel, col = on ? PAPER : INK;
          if (on) { c.fillStyle = INK; c.fillRect(0, y, W - 5, rh - 1); }
          T(c, it.label, 6, y + 2, 12, { w: on ? 700 : 600, c: col, max: Math.round(W * 0.52) });
          drawVal(c, it, W - 10, y + 4, 10, col, 1);
          if (hit) hit(0, y, W, rh, () => ctl.listTap(i));
        });
        scrollbar(c, W - 3, y0, H - y0 - 2, off, vis, items.length);
        return;
      }
      const sb = statusBar(c, dev, m, title, true, hit, ctl);
      const pad = mt.pad, y0 = sb + Math.round(pad / 2), bottom = H - mt.hint - 2, rh = mt.row, vis = Math.max(1, Math.floor((bottom - y0) / rh));
      const listW = dev.cls === 'M' ? W : Math.round(W * 0.56);
      const off = clamp(sel - Math.floor(vis / 2), 0, Math.max(0, items.length - vis));
      const vs = mt.f3 >= 14 ? 2 : 1;
      items.slice(off, off + vis).forEach((it, q) => {
        const i = off + q, y = y0 + q * rh, on = i === sel, col = on ? PAPER : INK;
        if (on) { c.fillStyle = dev.gray ? '#2a2a2a' : INK; c.fillRect(Math.round(pad / 2), y + 2, listW - pad - 4, rh - 4); }
        else if (i + 1 !== sel && q < vis - 1) dotsH(c, pad, listW - pad - 4, y + rh - 1);
        T(c, it.label, pad + 6, y + Math.round((rh - mt.f2) / 2), mt.f2, { w: on ? 700 : 600, c: col, max: Math.round(listW * 0.5) });
        drawVal(c, it, listW - pad - 12, y + Math.round((rh - mt.f3) / 2), mt.f3, col, vs);
        if (hit) hit(0, y, listW, rh, () => ctl.listTap(i));
      });
      scrollbar(c, listW - pad, y0, bottom - y0, off, vis, items.length);
      if (dev.cls !== 'M') {
        const it = items[sel], hx = listW + pad * 2, hw = W - hx - pad;
        dotsV(c, listW + pad, y0 + 4, bottom - 6);
        if (it) {
          T(c, it.label.toUpperCase(), hx, y0 + pad, mt.f3, { w: 700, max: hw });
          if (it.help) wrapT(c, it.help, hx, y0 + pad + mt.f3 + 10, mt.f2 - 2, hw, Math.max(1, Math.floor((bottom - y0 - mt.f3 - pad * 3) / (mt.f2 + 2))), mt.f2 + 2, { w: 500 });
        }
      }
      hints(c, dev, hintSet(dev, 'list'));
    }

    /* ---------- Altre schermate ---------- */
    function scanning(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Reti vicine', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const ms = S ? 2 : Math.max(3, Math.floor((H - t0 - mt.hint) * 0.4 / 25)), cx = Math.round(W / 2), my = t0 + (S ? 8 : mt.pad);
      figure(c, m, null, cx - Math.round(AV_W * ms / 2), my, ms, 'working', f);
      T(c, 'Ricerca reti…', cx, my + 25 * ms + (S ? 6 : mt.pad), S ? 12 : mt.f2, { w: 700, a: 'center' });
    }
    function qrScreen(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Configura Wi-Fi', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev), tp = top(m);
      const N = 29;  // versione 3, come il QR vero (WIFI:T:WPA;S:…;P:…;;) del firmware
      const avail = H - t0 - (S ? 4 : mt.hint + mt.pad * 2);
      const px = Math.max(2, Math.min(Math.floor(avail / (N + 2)), S ? 3 : Math.floor(W * 0.42 / (N + 2))));
      const x = (S ? 4 : mt.pad * 2) + px, y = t0 + px + (S ? 0 : mt.pad);
      drawQR(c, x, y, px, 'calamaio-3f2a', N);
      const tx = x + (N + 1) * px + (S ? 8 : mt.pad * 2), tw = W - tx - (S ? 4 : mt.pad);
      if (S) {
        T(c, 'INQUADRA IL QR', tx, t0 + 2, 8, { f: F_PIX, w: 700, max: tw });
        ['1. RETE CALAMAIO-3F2A', '2. APRI 192.168.4.1', '3. ' + (tp.ssid ? 'SCEGLI ' + tp.ssid.toUpperCase() : 'SCEGLI LA RETE'), '4. SCRIVI LA PASSWORD'].forEach((l, q) => T(c, l, tx, t0 + 18 + q * 13, 8, { f: F_PIX, w: 400, max: tw }));
        // Password della rete Calamaio-3F2A per chi non inquadra il QR (qui finta: 2× simula ancora l'inserimento).
        T(c, 'PASSWORD ' + m.sys.apPass, tx, H - 12, 8, { f: F_PIX, w: 700, max: tw });
        return;
      }
      const fs = mt.f2, lh = fs + 6;
      let yy = y - px;
      T(c, 'Inquadra il QR', tx, yy, mt.f1, { w: 700, max: tw }); yy += mt.f1 + 10;
      ['1. Collegati alla rete Calamaio-3F2A', '2. Si apre la pagina di configurazione, oppure vai su 192.168.4.1', '3. ' + (tp.ssid ? 'Scegli ' + tp.ssid : 'Scegli la rete') + ' e scrivi la password'].forEach((l) => {
        wrapLines(c, l, fs, tw, 600).slice(0, 3).forEach((ln) => { T(c, ln, tx, yy, fs, { w: 600, max: tw }); yy += lh; });
        yy += 4;
      });
      T(c, 'Password ' + m.sys.apPass, tx, H - mt.hint - mt.f3 - mt.pad, mt.f3, { w: 700, max: tw });
      hints(c, dev, hintSet(dev, 'qr'));
    }
    function connecting(c, dev, m, f, hit, ctl) {
      const tp = top(m), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const y0 = S ? 0 : statusBar(c, dev, m, 'Wi-Fi', false, null, ctl);
      const ms = S ? 3 : Math.max(3, Math.floor((H - y0 - mt.hint) * 0.45 / 25)), cx = Math.round(W / 2), my = y0 + (S ? 6 : mt.pad * 2);
      figure(c, m, null, cx - Math.round(AV_W * ms / 2), my, ms, 'working', f);
      T(c, 'Connessione a ' + tp.ssid + '.'.repeat(1 + (Math.abs(f) % 3)), cx, my + 25 * ms + (S ? 4 : mt.pad), S ? 12 : mt.f2, { w: 700, a: 'center', max: W - 20 });
    }
    function oggi(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Oggi', hit, ctl), W = dev.W, H = dev.H, mt = MET(dev), S = dev.cls === 'S';
      const st = m.stats, avg = st.waitN ? Math.round(st.waitSum / st.waitN / 60000) : 0;
      const tiles = [[String(st.tasks), 'TASK FATTI'], [String(st.permDisplay), 'OK DAL DISPLAY'], [String(st.permTerminal), 'DAL TERMINALE'], [avg + ' min', 'ATTESA MEDIA'], ['+' + st.linesAdd + ' −' + st.linesDel, 'RIGHE'], [st.cost.toFixed(2).replace('.', ',') + ' $', 'COSTO STIMATO']];
      const ban = !S && attention(m) && !quiet(m) ? mt.f3 + 14 : 0;
      const cols = (S || dev.cls === 'M') ? 2 : 3, rows = Math.ceil(tiles.length / cols), bottom = H - (S ? 0 : mt.hint) - ban, gw = W / cols, gh = (bottom - t0) / rows;
      tiles.forEach((tl, k) => {
        const cx = (k % cols) * gw + gw / 2, y = t0 + Math.floor(k / cols) * gh;
        const vs = S ? 16 : Math.round(mt.f1 * 1.6), ls = S ? 8 : mt.f3;
        T(c, tl[0], cx, y + gh * 0.5 - vs * 0.75, vs, { w: 700, a: 'center', max: gw - 10 });
        T(c, tl[1], cx, y + gh * 0.5 + vs * 0.4, ls, { f: S ? F_PIX : F_UI, w: 700, a: 'center', max: gw - 10 });
        if (k % cols < cols - 1) dotsV(c, Math.round((k % cols + 1) * gw), y + 6, y + gh - 6);
      });
      for (let r = 1; r < rows; r++) dotsH(c, 4, W - 4, Math.round(t0 + gh * r));
      hints(c, dev, hintSet(dev, 'view'));
    }
    function storico(c, dev, m, f, hit, ctl) {
      const tp = top(m), t0 = viewHeader(c, dev, m, dev.sd ? 'Storico · SD' : 'Storico', hit, ctl), W = dev.W, H = dev.H, mt = MET(dev), S = dev.cls === 'S';
      const fs = S ? 10 : mt.f3, lh = fs + (S ? 4 : 7), bottom = H - (S ? 0 : mt.hint) - 2, vis = Math.max(1, Math.floor((bottom - t0) / lh));
      const maxOff = Math.max(0, m.log.length - vis);
      tp.off = Math.min(tp.off || 0, maxOff);
      const tx = S ? 4 : mt.pad, nx = tx + (S ? 30 : Math.round(fs * 3.6)), ex = nx + (S ? 72 : Math.round(fs * 9));
      m.log.slice(tp.off, tp.off + vis).forEach((e, q) => {
        const y = t0 + q * lh;
        T(c, hm(e.t), tx, y, fs, { w: 700 });
        T(c, e.name, nx, y, fs, { w: 700, max: ex - nx - 6 });
        T(c, e.text, ex, y, fs, { w: 500, max: W - ex - 10 });
      });
      scrollbar(c, W - 4, t0, bottom - t0, tp.off, vis, m.log.length);
      hints(c, dev, hintSet(dev, 'scroll'));
    }
    function batteria(c, dev, m, f, hit, ctl) {
      const S = dev.cls === 'S', mt = MET(dev), W = dev.W, H = dev.H, t0 = viewHeader(c, dev, m, 'Batteria', hit, ctl);
      const pct = Math.round(m.sys.batt), save = powerSave(m), s = S ? 3 : Math.max(4, Math.round(H / 70));
      const x = S ? 8 : mt.pad * 2, y = t0 + (S ? 6 : mt.pad);
      battIcon(c, x, y, s, pct, INK);
      if (m.sys.usb) glyph(c, G.bolt, x + 18 * s, y, s, INK);
      const fsBig = S ? 22 : Math.round(8 * s * 1.3);
      T(c, pct + '%', x + (m.sys.usb ? 25 : 19) * s, y + Math.round(4 * s - fsBig / 2), fsBig, { w: 700 });
      const fs = S ? 10 : mt.f2, lh = fs + (S ? 4 : 8);
      const yy = y + 8 * s + (S ? 8 : Math.round(mt.pad * 1.5));
      const lines = [m.sys.usb ? 'In carica dalla USB-C' : 'A batteria', autonomyText(m), 'Risparmio energetico: ' + yes(save) + (m.sys.batt < 15 && !m.set.save && !m.sys.usb ? ' (automatico)' : '')];
      lines.forEach((l, k) => T(c, l, x, yy + k * lh, fs, { w: k === 2 ? 700 : 600, max: W - x * 2 }));
      if (hit) hit(0, yy + 2 * lh - 4, W, lh + 8, ctl.ok);
      if (S) T(c, '2X RISPARMIO   LUNGA INDIETRO', 4, 110, 8, { f: F_PIX, w: 400, max: W - 8 });
      else hints(c, dev, hintSet(dev, 'batt'));
    }
    function stato(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Stato', hit, ctl), W = dev.W, s = m.sys, tx = 48;
      wifiIcon(c, 10, t0 + 4, 2, s.wifi ? rssiBars(s.rssi) : -1, INK);
      T(c, s.wifi ? s.ssid : 'Nessuna rete', tx, t0 + 1, 13, { w: 700, max: W - tx - 4 });
      T(c, s.wifi ? s.rssi + ' dBm · ' + s.ip : 'Menu > Wi-Fi per configurare', tx, t0 + 16, 10, { w: 500, max: W - tx - 4 });
      battIcon(c, 8, t0 + 38, 2, s.batt, INK);
      T(c, Math.round(s.batt) + '%' + (s.usb ? ' · in carica' : ''), tx, t0 + 34, 13, { w: 700, max: W - tx - 4 });
      T(c, powerSave(m) ? 'risparmio energetico attivo' : 'circa 2 giorni con Wi-Fi acceso', tx, t0 + 49, 10, { w: 500, max: W - tx - 4 });
      glyph(c, G.plug, 12, t0 + 70, 2, INK);
      if (!s.pc) strike(c, 10, t0 + 68, 9, 2, INK, PAPER);
      T(c, s.pc ? 'PC collegato' : 'PC non risponde', tx, t0 + 68, 13, { w: 700, max: W - tx - 4 });
      T(c, s.pair === 'redo' ? 'Abbinamento: da rifare' : 'calamaio.local', tx, t0 + 83, 10, { w: 500, max: W - tx - 4 });
    }
    function sleepScreen(c, dev, m, f) {
      const W = dev.W, H = dev.H, S = dev.cls === 'S';
      const fs = Math.round(H * (S ? 0.36 : dev.cls === 'W' ? 0.4 : 0.28)), pad = S ? 8 : Math.round(H * 0.06);
      T(c, clock(m), pad, pad, fs, { w: 700 });
      const ds = Math.max(10, Math.round(fs * 0.24));
      T(c, dateStr(m), pad, pad + fs + Math.round(fs * 0.12), ds, { w: 600, max: Math.round(W * 0.6) });
      const ms = Math.max(2, Math.floor(H * 0.55 / 25));
      // Giornata di lavoro (0.9.0): attiva, o finita da meno di un'ora. Classe S: in alto a destra «MANCANO» e il conto
      // (come un secondo orologio), sotto «fino alle 18:00», la mascotte più in basso; sotto la data la barra della
      // giornata (dall'orario d'inizio alla fine). Alla fine «GIORNATA» / «FINITA», barra piena, mascotte contenta.
      const wk = m.work.on || workDone(m), done = !m.work.on;
      const wpose = done && wk ? 'done' : 'idle';
      if (wk && S) {
        const xr = W - pad, endHm = hm(new Date(m.work.endAt));
        T(c, done ? 'GIORNATA' : 'MANCANO', xr, pad + 2, 8, { f: F_PIX, w: 700, a: 'right' });
        T(c, done ? 'FINITA' : workLeftText(m, true), xr, pad + 12, 18, { w: 700, a: 'right' });
        T(c, (done ? 'alle ' : 'fino alle ') + endHm, xr, pad + 33, 10, { w: 600, a: 'right' });
        figure(c, m, null, W - AV_W * ms - pad, H - pad - 25 * ms - 4, ms, wpose, f);
        bigBar(c, pad, 74, W - AV_W * ms - pad * 3, 8, done ? 100 : workFrac(m) * 100);
      } else {
        figure(c, m, null, W - AV_W * ms - pad, Math.round((H - 25 * ms) / 2), ms, wpose, f);
        if (wk) {  // M, W, L: impaginazione provvisoria (non c'è ancora un firmware da seguire)
          const t = done ? 'Giornata finita' : 'Mancano ' + workLeftText(m, false) + ' · fino alle ' + hm(new Date(m.work.endAt));
          T(c, t, pad, pad + fs + Math.round(fs * 0.12) + ds + Math.round(ds * 0.6), ds, { w: 700, max: Math.round(W * 0.6) });
        }
      }
      const att = m.sessions.filter(isAttn).length;
      T(c, m.sessions.length + ' sessioni · ' + (att ? att + ' ti aspettano' : 'nessuna attesa'), pad, H - pad - ds, ds, { w: 600, max: Math.round(W * 0.6) });
      // M6: senza cavo il Riposo è sonno profondo; col cavo resta acceso. 0.10.1: in sonno l'ora si aggiorna con un risveglio
      // breve a ogni minuto (ogni 5 con il risparmio energetico), con un parziale e senza Wi-Fi; il Wi-Fi ogni 15 minuti.
      const ps = S ? 8 : Math.max(8, Math.round(ds * 0.8));
      T(c, m.sys.usb ? 'COL CAVO RESTO ACCESO' : 'UN TASTO MI SVEGLIA', pad, H - pad - ds - ps - 6, ps, { f: F_PIX, w: 700, max: Math.round(W * 0.6) });
    }
    // M6: batteria sotto la soglia critica senza cavo: ultima schermata prima del sonno profondo senza timer.
    function battLowScreen(c, dev, m, f) {
      const W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const ms = S ? 3 : Math.max(3, Math.floor(H * 0.45 / 25)), x0 = S ? 4 : mt.pad * 2, y0 = S ? 22 : Math.round((H - 25 * ms) / 2);
      figure(c, m, null, x0, y0, ms, 'tired', 0);
      const x = S ? 92 : x0 + 27 * ms + mt.pad * 3, w = W - x - 4, big = S ? 18 : mt.f1 + 6, fs = S ? 10 : mt.f2;
      let y = S ? 14 : y0;
      T(c, 'BATTERIA', x, y, big, { w: 700, max: w }); y += big + 2;
      T(c, 'SCARICA', x, y, big, { w: 700, max: w }); y += big + (S ? 6 : mt.pad);
      T(c, 'Collega la USB-C', x, y, fs, { w: 600, max: w }); y += fs + 4;
      T(c, 'poi premi il tasto', x, y, fs, { w: 600, max: w });
      T(c, 'SPENTO PER PROTEGGERLA', x, S ? 100 : H - mt.pad - 8, 8, { f: F_PIX, w: 700, max: w });
    }
    // M6: calibrazione della batteria (Menu › Batteria › Calibra la batteria).
    function calibScreen(c, dev, m, f) {
      const W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev), k = m.calib;
      const done = k.phase === 'done', wait = k.phase === 'wait';
      const ms = S ? 3 : Math.max(3, Math.floor(H * 0.45 / 25)), x0 = S ? 4 : mt.pad * 2, y0 = S ? 22 : Math.round((H - 25 * ms) / 2);
      figure(c, m, null, x0, y0, ms, done ? 'done' : 'working', f);
      const x = S ? 92 : x0 + 27 * ms + mt.pad * 3, w = W - x - 4, big = S ? 14 : mt.f1, fs = S ? 10 : mt.f2, px = 8;
      let y = S ? 6 : y0;
      const head = done ? 'CALIBRAZIONE FATTA' : wait ? 'CALIBRAZIONE' : 'CALIBRAZIONE · FASE ' + (k.phase === 'max' ? 1 : 2) + '/2';
      T(c, head, x, y, px, { f: F_PIX, w: 700, max: w }); y += px + 6;
      const name = done ? 'Fatto' : wait ? 'Stacca il cavo' : k.phase === 'max' ? 'Massimo consumo' : 'Attesa';
      T(c, name, x, y, big, { w: 700, max: w }); y += big + 6;
      const lines = [];
      if (done) {
        lines.push('Attivo ' + fmtRate(m.bat.cal.active), 'Attesa ' + fmtRate(m.bat.cal.idle));
      } else if (wait) {
        lines.push('La prova parte da sola', 'quando stacchi la USB-C');
      } else {
        const el = Math.floor(k.ms / 60000);
        lines.push(k.paused ? 'In pausa: stacca il cavo' : el + ' min · ne mancano ' + Math.max(0, 60 - el));
        const r = calRate(k.samples);
        lines.push((k.mv ? fmtV(k.mv) : '— V') + ' · ' + (r == null ? '%/h in corso' : fmtRate(r)));
      }
      lines.forEach((l) => { T(c, l, x, y, fs, { w: 600, max: w }); y += fs + 4; });
      T(c, done ? 'LUNGA INDIETRO' : '3X ANNULLA', x, S ? 100 : H - mt.pad - 8, 8, { f: F_PIX, w: 700, max: w });
    }
    function boot(c, dev, m, f) {
      const W = dev.W, H = dev.H, S = dev.cls === 'S', tp = top(m);
      const ms = S ? 3 : Math.max(3, Math.floor(H * 0.42 / 25)), el = clamp(1 - (tp.until - now()) / 2600, 0, 1);
      const bw = 27 * ms, x = S ? 8 : Math.round(W / 2 - bw - H * 0.04), y = Math.round((H - 25 * ms) / 2);
      figure(c, m, null, x, y, ms, el > 0.85 ? 'done' : 'working', f);
      const tx = x + bw + (S ? 10 : Math.round(H * 0.06)), fs = S ? 18 : Math.round(H * 0.09), ss = Math.round(fs * 0.55);
      T(c, 'CALAMAIO OS', tx, y + (S ? 4 : 0), fs, { w: 700, max: W - tx - 6 });
      T(c, 'versione 0.2', tx, y + fs + (S ? 8 : 6), ss, { w: 600 });
      const steps = [['Schermo', 0.15], ['Wi-Fi ' + (m.sys.wifi ? m.sys.ssid : 'da configurare'), 0.45], ['PC calamaio.local', 0.75]];
      steps.forEach((st, k) => {
        const yy = y + fs * 2 + (S ? 12 : 20) + k * Math.round(fs * 0.75);
        T(c, st[0] + (el >= st[1] ? '  ok' : '  …'), tx, yy, ss, { w: el >= st[1] ? 700 : 500, max: W - tx - 6 });
      });
    }
    function offline(c, dev, m, f, hit, ctl) {
      // 0.10.1: senza l'ora vera nessun orario inventato.
      const W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev), last = m.sys.wallOk === false ? '--:--' : hm(new Date(m.sys.lastPc || now()));
      if (S) {
        figure(c, m, null, 4, 22, 3, 'idle', f);
        const x = 92;
        T(c, 'PC NON', x, 14, 18, { w: 700 });
        T(c, 'RAGGIUNGIBILE', x, 34, 14, { w: 700, max: W - x - 4 });
        T(c, 'Ultimo dato alle ' + last, x, 56, 10, { w: 600, max: W - x - 4 });
        T(c, 'Riprovo ogni 30 secondi', x, 70, 10, { w: 600, max: W - x - 4 });
        T(c, '3X MENU', x, 100, 8, { f: F_PIX, w: 700 });
        alertsS(c, m, W - 4, 4);
        return;
      }
      const y0 = statusBar(c, dev, m, 'CLAUDE CODE', false, hit, ctl);
      const ms = Math.max(3, Math.floor((H - y0 - mt.hint) * 0.45 / 25)), cx = W / 2;
      figure(c, m, null, Math.round(cx - AV_W * ms / 2), y0 + mt.pad, ms, 'idle', f);
      let y = y0 + mt.pad + 25 * ms + mt.pad;
      T(c, 'PC NON RAGGIUNGIBILE', cx, y, mt.f1, { w: 700, a: 'center' }); y += mt.f1 + 6;
      T(c, 'Ultimo aggiornamento alle ' + last, cx, y, mt.f3 + 1, { w: 600, a: 'center' }); y += mt.f3 + 6;
      T(c, 'Riprovo ogni 30 secondi e intanto risparmio batteria', cx, y, mt.f3 + 1, { w: 600, a: 'center', max: W - 30 });
      hints(c, dev, hintSet(dev, 'view'));
    }
    /* ---------- schermate 0.2 ---------- */
    function bigBar(c, x, y, w, h, pct) {
      box(c, x, y, w, h, INK, 1);
      c.fillStyle = INK;
      c.fillRect(x + 2, y + 2, Math.round((w - 4) * clamp(pct, 0, 100) / 100), Math.max(1, h - 4));
      for (let k = 1; k < 4; k++) c.fillRect(x + Math.round(w * k / 4), y + h, 1, 3);
    }
    /* ---------- aggiornamento remoto (docs/OTA.md §1.6, AVATAR.md §12) ---------- */
    // Classe S: titolo e versioni, barra grande a tutta larghezza, la mascotte predefinita che corre sulla barra
    // (scala 2: 56×50, i piedi sul bordo superiore, il bordo destro sul fronte della parte riempita), percentuale
    // e byte, «NON SPEGNERE» e la fase. Errore e rifiuto: figura a sinistra e testo a destra come «Batteria scarica».
    const OTA_LABEL = { receive: 'RICEZIONE', verify: 'VERIFICA', write: 'SCRITTURA', reboot: 'RIAVVIO', done: 'FATTO' };
    const OTA_BAR = { x: 6, y: 72, w: 238, h: 14, s: 2 };
    const OTA_RESULT_MS = 15000;
    function kb(n) { return String(Math.round(n / 1024)).replace(/\B(?=(\d{3})+(?!\d))/g, ' '); }
    function otaPct(o) { return o.phase === 'receive' ? (o.total ? Math.floor(o.done * 100 / o.total) : 0) : 100; }
    // Fotogramma della corsa: il passo k (uno per ridisegno), mod 4 sul profilo fluido e mod 2 sul ridotto.
    function otaFrame(dev, o) { return o.step % (dev.fps >= 3 ? 4 : 2); }
    function otaScreen(c, dev, m, f) {
      const o = m.ota, W = dev.W, B = OTA_BAR;
      if (o.phase === 'error' || o.phase === 'rejected') { otaResult(c, dev, m); return; }
      T(c, 'AGGIORNAMENTO', 6, 4, 14, { w: 700 });
      T(c, o.from + ' → ' + o.to, W - 6, 7, 10, { w: 600, a: 'right' });
      const pct = otaPct(o), end = o.phase === 'reboot' || o.phase === 'done';
      bigBar(c, B.x, B.y, B.w, B.h, pct);
      // AVATAR.md §12.2: bordo destro (colonna 27) sul fronte, riga 24 sul bordo; sotto 56 px resta al bordo sinistro.
      const front = B.x + 2 + Math.round((B.w - 4) * clamp(pct, 0, 100) / 100);
      const ox = clamp(front - AV_W * B.s, 0, W - AV_W * B.s), oy = B.y - AV_H * B.s;
      const a = AVATARS[m.set.defaultAvatar] || AVATARS.goccia;
      if (a) { drawAvatar(c, a, ox, oy, B.s, end ? 'done' : 'run', otaFrame(dev, o)); accOverlay(c, m, a, ox, oy, B.s, end ? 'done' : 'run', otaFrame(dev, o)); }
      T(c, end ? 'Riavvio…' : pct + '%', 6, 92, 16, { w: 700 });
      T(c, kb(o.done) + ' / ' + kb(o.total) + ' KB', W - 6, 96, 10, { w: 600, a: 'right' });
      T(c, 'NON SPEGNERE', 6, 111, 8, { f: F_PIX, w: 700 });
      T(c, OTA_LABEL[o.phase] || '', W - 6, 111, 8, { f: F_PIX, w: 700, a: 'right' });
    }
    function otaResult(c, dev, m) {
      const o = m.ota, W = dev.W, err = o.phase === 'error', x = 92, w = W - x - 4;
      figure(c, m, null, 4, 22, 3, err ? 'error' : 'idle', 0);
      T(c, 'AGGIORNAMENTO', x, 6, 8, { f: F_PIX, w: 700, max: w });
      T(c, err ? 'NON RIUSCITO' : 'RIFIUTATO', x, 18, 16, { w: 700, max: w });
      let y = 40;
      if (err) { T(c, 'Resta la versione di prima', x, y, 10, { w: 600, max: w }); y += 14; }
      wrapT(c, o.why, x, y, 10, w, err ? 3 : 4, 13, { w: 600 });
      T(c, 'LUNGA INDIETRO', x, 100, 8, { f: F_PIX, w: 700, max: w });
    }
    function resetLabel(ts) {
      const d = new Date(ts);
      if (d.toDateString() === new Date().toDateString()) return 'si azzera alle ' + hm(d);
      try { return 'si azzera ' + d.toLocaleDateString('it-IT', { weekday: 'long' }) + ' alle ' + hm(d); } catch (e) { return 'si azzera alle ' + hm(d); }
    }
    function limiti(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Limiti', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev), L = m.limits;
      const rows = [['Ultime 5 ore', L.h5, L.h5Reset], ['Ultimi 7 giorni', L.d7, L.d7Reset]];
      if (S) {
        rows.forEach((r, k) => {
          const y = t0 + 2 + k * 38;
          const has = r[1] != null;  // dal PC una finestra può mancare
          T(c, r[0].toUpperCase(), 6, y, 8, { f: F_PIX, w: 700 });
          T(c, has ? Math.round(r[1]) + '%' : '—', W - 6, y - 2, 13, { w: 700, a: 'right' });
          bigBar(c, 6, y + 12, W - 12, 9, has ? r[1] : 0);
          T(c, has ? resetLabel(r[2]) : 'nessun dato dal PC', 6, y + 25, 10, { w: 600, max: W - 12 });
        });
        if (!m.sessions.length) return;
        let hi = m.sessions[0];
        m.sessions.forEach((s) => { if (s.ctx > hi.ctx) hi = s; });
        T(c, 'CONTESTO: ' + hi.name.toUpperCase() + ' ' + hi.ctx + '%', 6, H - 11, 8, { f: F_PIX, w: 400, max: W - 12 });
        return;
      }
      const half = dev.cls === 'M' ? W : Math.round(W * 0.5), pad = mt.pad, x = pad * 2, w = half - pad * 4, bh = Math.round(mt.f2 * 1.1);
      let y = t0;
      rows.forEach((r) => {
        const has = r[1] != null;
        T(c, r[0], x, y, mt.f2, { w: 700 });
        T(c, has ? Math.round(r[1]) + '%' : '—', x + w, y - 2, mt.f1, { w: 700, a: 'right' });
        y += mt.f1 + 4;
        bigBar(c, x, y, w, bh, has ? r[1] : 0);
        y += bh + 8;
        T(c, has ? resetLabel(r[2]) : 'nessun dato dal PC', x, y, mt.f3, { w: 600, max: w });
        y += mt.f3 + Math.round(pad * 1.5);
      });
      const cx0 = dev.cls === 'M' ? x : half + pad * 2, cw = dev.cls === 'M' ? w : W - cx0 - pad * 2;
      let cy = dev.cls === 'M' ? y : t0;
      if (dev.cls !== 'M') dotsV(c, half, t0, H - mt.hint - 6);
      T(c, 'CONTESTO PER SESSIONE', cx0, cy, mt.f3, { w: 700 });
      cy += mt.f3 + 6;
      const rh = mt.f3 + 9;
      m.sessions.forEach((s) => {
        if (cy + rh > H - mt.hint - 2) return;
        T(c, s.name, cx0, cy, mt.f3, { w: 600, max: Math.round(cw * 0.45) });
        const bx = cx0 + Math.round(cw * 0.48), bw = cw - Math.round(cw * 0.48) - Math.round(mt.f3 * 3.2);
        bigBar(c, bx, cy + 1, bw, Math.max(6, mt.f3 - 2), s.ctx);
        T(c, s.ctx + '%', cx0 + cw, cy, mt.f3, { w: s.ctx >= 80 ? 700 : 600, a: 'right' });
        cy += rh;
      });
      hints(c, dev, hintSet(dev, 'view'));
    }
    function focusScreen(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Concentrazione', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev), on = focusOn(m);
      const left = on ? Math.max(0, Math.ceil((m.focus.until - now()) / 60000)) : m.focus.len;
      const ms = S ? 3 : Math.max(3, Math.floor((H - t0 - mt.hint) * 0.5 / 25));
      const mx = S ? 4 : mt.pad * 2, my = t0 + (S ? 4 : mt.pad);
      figure(c, m, null, mx, my, ms, on ? 'working' : 'idle', f);
      const tx = mx + 27 * ms + (S ? 10 : mt.pad * 2), tw = W - tx - (S ? 4 : mt.pad * 2);
      const big = S ? 40 : Math.round((H - t0) * 0.3), fs = S ? 10 : mt.f2;
      T(c, left + ' min', tx, my, big, { w: 700, max: tw });
      let y = my + big + (S ? 6 : mt.pad);
      const lines = on
        ? (S ? ['Avvisi in silenzio', 'fino alle ' + hm(new Date(m.focus.until))] : ['Avvisi in silenzio fino alle ' + hm(new Date(m.focus.until)), 'Il display si aggiorna una volta al minuto'])
        : ['Pronto per ' + m.focus.len + ' minuti'];
      lines.forEach((l) => { wrapLines(c, l, fs, tw, 600).slice(0, 2).forEach((ln) => { T(c, ln, tx, y, fs, { w: 600, max: tw }); y += fs + 4; }); });
      if (S) T(c, on ? '2X FERMA   LUNGA INDIETRO' : '2X INIZIA   LUNGA INDIETRO', 4, 110, 8, { f: F_PIX, w: 400, max: W - 8 });
      else hints(c, dev, hintSet(dev, 'focus'));
      if (hit) hit(0, t0, W, H - t0 - mt.hint, ctl.ok);
    }
    // Gioco «Alto o basso» (0.9.0). Carta a sinistra: riquadro con angoli arrotondati in pixel (bordo 2 px), valore grande
    // con cifre e lettere 5×7 a scala 5 (A, 2…10, J, Q, K), indice piccolo in due angoli. Al centro la serie, il record e
    // l'esito; a destra la mascotte predefinita (input: domanda, done: giusto, idle: pari, error: sbagliato).
    const CARD_GLYPHS = {
      A: ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
      2: ['.###.', '#...#', '....#', '...#.', '..#..', '.#...', '#####'],
      3: ['####.', '....#', '....#', '.###.', '....#', '....#', '####.'],
      4: ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
      5: ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
      6: ['..##.', '.#...', '#....', '####.', '#...#', '#...#', '.###.'],
      7: ['#####', '....#', '...#.', '..#..', '.#...', '.#...', '.#...'],
      8: ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
      9: ['.###.', '#...#', '#...#', '.####', '....#', '...#.', '.##..'],
      1: ['.#.', '##.', '.#.', '.#.', '.#.', '.#.', '###'],
      0: ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
      J: ['..###', '...#.', '...#.', '...#.', '#..#.', '#..#.', '.##..'],
      Q: ['.###.', '#...#', '#...#', '#...#', '#.#.#', '#..#.', '.##.#'],
      K: ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#']
    };
    const CARD = { x: 8, w: 58, h: 86, s: 5 };
    function cardRank(c, txt, cx, cy, s) {
      const gs = txt.split('').map((ch) => CARD_GLYPHS[ch]);
      const cols = gs.reduce((a, g) => a + g[0].length, 0) + gs.length - 1;
      let x = cx - Math.floor(cols * s / 2);
      const y = cy - Math.floor(7 * s / 2);
      c.fillStyle = INK;
      gs.forEach((g) => {
        g.forEach((row, r) => { for (let q = 0; q < row.length; q++) if (row[q] === '#') c.fillRect(x + q * s, y + r * s, s, s); });
        x += (g[0].length + 1) * s;
      });
    }
    function cardBox(c, x, y, w, h) {
      c.fillStyle = INK;
      c.fillRect(x + 3, y, w - 6, 2); c.fillRect(x + 3, y + h - 2, w - 6, 2);
      c.fillRect(x, y + 3, 2, h - 6); c.fillRect(x + w - 2, y + 3, 2, h - 6);
      c.fillRect(x + 1, y + 1, 2, 2); c.fillRect(x + w - 3, y + 1, 2, 2);
      c.fillRect(x + 1, y + h - 3, 2, 2); c.fillRect(x + w - 3, y + h - 3, 2, 2);
    }
    function gameScreen(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Alto o basso', hit, ctl), W = dev.W, S = dev.cls === 'S', g = m.game, r = RANKS[g.card];
      const y = t0 + 3;
      cardBox(c, CARD.x, y, CARD.w, CARD.h);
      T(c, r, CARD.x + 5, y + 5, 8, { f: F_PIX, w: 700 });
      T(c, r, CARD.x + CARD.w - 5, y + CARD.h - 13, 8, { f: F_PIX, w: 700, a: 'right' });
      cardRank(c, r, CARD.x + Math.floor(CARD.w / 2), y + Math.floor(CARD.h / 2), CARD.s);
      figure(c, m, null, W - AV_W * 2 - 6, t0 + 4, 2, gamePose(g), f);
      const tx = CARD.x + CARD.w + 10, tw = W - AV_W * 2 - 10 - tx;
      T(c, g.over ? 'FINE · SERIE' : 'SERIE', tx, t0 + 4, 8, { f: F_PIX, w: 700, max: tw });
      T(c, String(g.streak), tx, t0 + 14, 40, { w: 700, max: tw });
      if (g.over && g.streak > g.startBest) T(c, 'NUOVO RECORD!', tx, t0 + 58, 8, { f: F_PIX, w: 700, max: tw });
      else T(c, 'record ' + g.best, tx, t0 + 56, 10, { w: 600, max: tw });
      const pr = RANKS[g.prev], res = g.last === 'win' ? 'GIUSTO: ' + pr + ' → ' + r : g.last === 'tie' ? 'PARI: ' + pr + ' e ' + r : g.last === 'lose' ? 'SBAGLIATO: ' + pr + ' → ' + r : '';
      if (res) T(c, res, tx, t0 + 72, 13, { w: 700, max: W - tx - 6 });
      else T(c, 'Più alta o più bassa?', tx, t0 + 73, 12, { w: 600, max: W - tx - 6 });
      if (S) T(c, g.over ? '1X O 2X ANCORA   LUNGA ESCI' : '1X ALTA   2X BASSA   LUNGA ESCI', 4, 110, 8, { f: F_PIX, w: 400, max: W - 8 });
      else hints(c, dev, hintSet(dev, 'view'));
      if (hit) hit(0, t0, W, dev.H - t0, ctl.ok);
    }
    // «La mascotte» (gioco, 0.10.0): a sinistra la mascotte predefinita con l'accessorio (stanca se il gioco lo dice,
    // altrimenti contenta); a destra livello, barra degli XP del livello, XP ed energia, ultimi 3 traguardi. 2× apre
    // «Accessorio». Senza dati dal PC (pet.known falso) livello «—».
    function petScreen(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'La mascotte', hit, ctl), W = dev.W, S = dev.cls === 'S', p = m.pet;
      const ms = S ? 3 : Math.max(3, Math.floor((dev.H - t0 - MET(dev).hint) * 0.5 / 25));
      figure(c, m, null, S ? 2 : MET(dev).pad * 2, t0 + 2, ms, p.tired && petOn(m) ? 'tired' : 'done', f);
      const tx = (S ? 2 : MET(dev).pad * 2) + AV_W * ms + 6, tw = W - tx - 6;
      T(c, p.known ? 'LIVELLO ' + p.level : 'LIVELLO —', tx, t0 + 1, 14, { w: 700, max: tw });
      box(c, tx, t0 + 19, tw, 7, INK, 1);
      if (p.known) { c.fillStyle = INK; c.fillRect(tx + 2, t0 + 21, Math.round((tw - 4) * petFrac(p)), 3); }
      if (p.known) {
        T(c, (p.xp - p.xpLevelStart) + '/' + (p.xpNext - p.xpLevelStart) + ' XP', tx, t0 + 29, 10, { w: 600, max: Math.round(tw / 2) });
        T(c, (p.tired && petOn(m) ? 'stanca ' : 'energia ') + p.energy + '%', tx + tw, t0 + 29, 10, { w: 600, a: 'right', max: Math.round(tw / 2) });
      } else T(c, 'Dati dal PC non ancora arrivati', tx, t0 + 29, 10, { w: 600, max: tw });
      T(c, 'ULTIMI TRAGUARDI', tx, t0 + 43, 8, { f: F_PIX, w: 700, max: tw });
      // Accessorio speciale da chiedere a Claude: una riga in neretto al posto del terzo traguardo.
      const sp = p.special && petOn(m), ng = sp ? Math.min(2, p.goals.length) : p.goals.length;
      if (!p.goals.length) T(c, 'Ancora nessuno', tx, t0 + 54, 10, { w: 500, max: tw });
      p.goals.slice(0, ng).forEach((g, k) => T(c, g, tx, t0 + 54 + k * 11, 10, { w: 500, max: tw }));
      if (sp) T(c, 'Speciale: chiedilo a Claude', tx, t0 + 54 + Math.max(1, ng) * 11, 10, { w: 700, max: tw });
      if (S) T(c, '2X ACCESSORIO   LUNGA INDIETRO', 4, 110, 8, { f: F_PIX, w: 400, max: W - 8 });
      else hints(c, dev, hintSet(dev, 'list'));
      if (hit) hit(0, t0, W, dev.H - t0, ctl.ok);
    }
    function guest(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Wi-Fi ospiti', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const avail = H - t0 - (S ? 4 : mt.hint + mt.pad * 2);
      const px = Math.max(2, Math.min(Math.floor(avail / 27), S ? 3 : Math.floor(W * 0.42 / 27)));
      const x = (S ? 4 : mt.pad * 2) + px, y = t0 + px + (S ? 0 : mt.pad);
      drawQR(c, x, y, px, 'ospiti-wifi');
      const tx = x + 26 * px + (S ? 8 : mt.pad * 2), tw = W - tx - (S ? 4 : mt.pad), f1 = S ? 16 : mt.f1, f2 = S ? 11 : mt.f2;
      let yy = y - px;
      T(c, 'Rete ospiti', tx, yy, f1, { w: 700, max: tw }); yy += f1 + (S ? 6 : 10);
      T(c, 'Ospiti', tx, yy, f2 + 2, { w: 700, max: tw }); yy += f2 + (S ? 6 : 10);
      wrapLines(c, 'Inquadra il codice con la fotocamera per collegarti senza scrivere la password.', f2, tw, 500).slice(0, S ? 3 : 4).forEach((ln) => { T(c, ln, tx, yy, f2, { w: 500, max: tw }); yy += f2 + 3; });
      T(c, S ? 'QR DI ESEMPIO' : 'QR di esempio', tx, H - (S ? 12 : mt.hint + mt.f3 + mt.pad), S ? 8 : mt.f3, { f: S ? F_PIX : F_UI, w: 600, max: tw });
      hints(c, dev, hintSet(dev, 'view'));
    }
    function servizi(c, dev, m, f, hit, ctl) {
      const tp = top(m), t0 = viewHeader(c, dev, m, 'Servizi e PR', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const rows = [{ head: 'SERVIZI' }].concat(
        m.services.map((s) => ({ ok: s.ok ? 'ok' : 'ko', a: s.name, b: s.ok ? s.ms + ' ms' : 'non risponde' })),
        [{ head: 'PULL REQUEST' }],
        m.prs.map((p) => ({ ok: p.checks, a: p.repo + ' #' + p.n, b: p.checks === 'run' ? 'controlli in corso' : p.title }))
      );
      const fs = S ? 10 : mt.f3 + 1, lh = fs + (S ? 5 : 9), bottom = H - (S ? 0 : mt.hint) - 2, vis = Math.max(1, Math.floor((bottom - t0) / lh));
      tp.off = clamp(tp.off || 0, 0, Math.max(0, rows.length - vis));
      const x = S ? 4 : mt.pad, s = fs >= 14 ? 2 : 1;
      rows.slice(tp.off, tp.off + vis).forEach((r, q) => {
        const y = t0 + q * lh;
        if (r.head) {
          T(c, r.head, x, y + 1, S ? 8 : mt.f3 - 1, { f: S ? F_PIX : F_UI, w: 700 });
          c.font = '700 ' + (S ? 8 : mt.f3 - 1) + 'px ' + (S ? F_PIX : F_UI);
          dotsH(c, x + Math.ceil(c.measureText(r.head).width) + 8, W - x - 8, y + Math.round(fs / 2));
          return;
        }
        glyph(c, r.ok === 'ok' ? ICONS.done : r.ok === 'ko' ? ICONS.error : ICONS.working0, x, y + Math.round((fs - 7 * s) / 2), s, INK);
        T(c, r.a, x + 7 * s + 6, y, fs, { w: 700, max: Math.round(W * 0.4) });
        T(c, r.b, W - x - 8, y, fs, { w: 500, a: 'right', max: Math.round(W * 0.45) });
      });
      scrollbar(c, W - 4, t0, bottom - t0, tp.off, vis, rows.length);
      hints(c, dev, hintSet(dev, 'scroll'));
    }
    function agenda(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Agenda', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const items = m.agenda.filter((a) => a.t > now() - 30 * 60000);
      if (!items.length) { T(c, 'Nessun appuntamento', W / 2, t0 + 20, S ? 12 : mt.f2, { w: 700, a: 'center' }); hints(c, dev, hintSet(dev, 'view')); return; }
      const nx = items[0], mins = Math.max(0, Math.round((nx.t - now()) / 60000));
      const x = S ? 6 : mt.pad * 2, f1 = S ? 22 : Math.round(mt.f1 * 1.6), f2 = S ? 12 : mt.f2, col2 = S ? 70 : f2 * 7;
      let y = t0 + (S ? 2 : mt.pad);
      const when = hm(new Date(nx.t));
      T(c, when, x, y, f1, { w: 700 });
      c.font = '700 ' + f1 + 'px ' + F_UI;
      T(c, mins < 60 ? 'tra ' + mins + ' min' : 'tra ' + Math.floor(mins / 60) + ' h ' + pad2(mins % 60), x + Math.ceil(c.measureText(when).width) + 10, y + f1 - f2 - 2, f2, { w: 600 });
      y += f1 + 4;
      T(c, nx.title, x, y, f2 + 2, { w: 700, max: W - x * 2 });
      y += f2 + (S ? 8 : mt.pad + 4);
      dotsH(c, x, W - x, y);
      y += S ? 5 : 10;
      items.slice(1, S ? 3 : 6).forEach((a) => {
        const d = new Date(a.t), same = d.toDateString() === new Date().toDateString();
        T(c, (same ? '' : 'domani ') + hm(d), x, y, f2, { w: 700 });
        T(c, a.title, x + col2, y, f2, { w: 500, max: W - x * 2 - col2 });
        y += f2 + (S ? 4 : 8);
      });
      T(c, S ? 'DA UN CALENDARIO ICS' : 'Da un calendario ICS, letto dal servizio sul PC', x, H - (S ? 11 : mt.hint + mt.f3 + mt.pad), S ? 8 : mt.f3, { f: S ? F_PIX : F_UI, w: 400, max: W - x * 2 });
      hints(c, dev, hintSet(dev, 'view'));
    }
    function ambiente(c, dev, m, f, hit, ctl) {
      const t0 = viewHeader(c, dev, m, 'Ambiente', hit, ctl), W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const hasCo2 = !!m.acc, hasTh = !!(dev.sensor || m.acc);
      if (!hasTh) {
        const ms = S ? 2 : 4, mx = S ? 6 : mt.pad * 2;
        figure(c, m, null, mx, t0 + 6, ms, 'idle', f);
        const tx = mx + 27 * ms + (S ? 8 : mt.pad * 2), tw = W - tx - 6, fs = S ? 11 : mt.f2;
        let y = t0 + 4;
        T(c, 'Nessun sensore', tx, y, S ? 14 : mt.f1, { w: 700, max: tw });
        y += (S ? 14 : mt.f1) + 8;
        wrapLines(c, 'Collega un sensore di CO2, temperatura e umidità ai pin I2C, per esempio un SCD40, e attiva Accessori.', fs, tw, 500).slice(0, S ? 5 : 6).forEach((ln) => { T(c, ln, tx, y, fs, { w: 500, max: tw }); y += fs + 3; });
        hints(c, dev, hintSet(dev, 'view'));
        return;
      }
      const co2 = Math.round(m.air.co2), bad = hasCo2 && co2 > 1000;
      const ms = S ? 3 : Math.max(3, Math.floor((H - t0 - mt.hint) * 0.5 / 25)), mx = S ? 4 : mt.pad * 2;
      figure(c, m, null, mx, t0 + (S ? 4 : mt.pad), ms, bad ? 'stuffy' : 'idle', f);
      const tx = mx + 27 * ms + (S ? 10 : mt.pad * 3), tw = W - tx - 6, big = S ? 26 : Math.round(mt.f1 * 1.8), fs = S ? 11 : mt.f2;
      let y = t0 + (S ? 2 : mt.pad);
      if (hasCo2) {
        T(c, co2 + ' ppm', tx, y, big, { w: 700, max: tw }); y += big + 4;
        T(c, bad ? 'Apri la finestra' : co2 > 800 ? 'Aria discreta' : 'Aria buona', tx, y, fs + 2, { w: 700, max: tw }); y += fs + 2 + (S ? 8 : mt.pad);
      } else {
        T(c, 'CO2 non misurata', tx, y, fs + 2, { w: 700, max: tw }); y += fs + 2 + (S ? 8 : mt.pad);
      }
      T(c, fmt1(m.sys.temp) + ' °C  ·  ' + Math.round(m.sys.rh) + '% umidità', tx, y, fs, { w: 600, max: tw }); y += fs + 6;
      T(c, dev.sensor ? 'Sensore interno SHT40' + (hasCo2 ? ' e CO2 esterno' : '') : 'Sensore esterno sui pin I2C', tx, y, S ? 10 : mt.f3, { w: 500, max: tw });
      hints(c, dev, hintSet(dev, 'view'));
    }
    function riepilogo(c, dev, m, f, hit, ctl) {
      const W = dev.W, H = dev.H, S = dev.cls === 'S', mt = MET(dev);
      const fin = m.sessions.filter((s) => s.state === 'done').length, wait = m.sessions.filter(isAttn), off = hm(new Date(m.sys.offAt || now()));
      const last = m.sessions.filter((s) => s.summary).slice(-1)[0];
      const waitTxt = wait.length === 0 ? 'nessuna ti aspettava' : wait.length === 1 ? '1 ti aspettava' : wait.length + ' ti aspettavano';
      if (S) {
        c.fillStyle = INK; c.fillRect(0, 0, W, 15);
        T(c, 'PC SPENTO ALLE ' + off, 4, 4, 8, { f: F_PIX, w: 700, c: PAPER, max: W - 8 });
        figure(c, m, null, 2, 22, 3, 'idle', 0);
        const x = 90, w = W - x - 4;
        T(c, fin + (fin === 1 ? ' finita' : ' finite'), x, 20, 16, { w: 700, max: w });
        T(c, waitTxt, x, 40, 12, { w: 600, max: w });
        if (wait.length) T(c, wait[0].name + ' · ' + STATES[wait[0].state].long.toLowerCase(), x, 56, 10, { w: 500, max: w });
        if (last) wrapT(c, last.name + ': ' + last.summary, x, 72, 10, w, 3, 12, { w: 500 });
        return;
      }
      const sb = statusBar(c, dev, m, 'PC spento alle ' + off, false, hit, ctl);
      const ms = Math.max(3, Math.floor((H - sb - mt.hint) * 0.5 / 25));
      figure(c, m, null, mt.pad * 2, sb + mt.pad, ms, 'idle', 0);
      const x = mt.pad * 4 + 27 * ms, w = W - x - mt.pad * 2, big = Math.round(mt.f1 * 1.4);
      let y = sb + mt.pad;
      T(c, fin + (fin === 1 ? ' sessione finita' : ' sessioni finite'), x, y, big, { w: 700, max: w }); y += big + 8;
      T(c, waitTxt.charAt(0).toUpperCase() + waitTxt.slice(1), x, y, mt.f2, { w: 700, max: w }); y += mt.f2 + 6;
      wait.slice(0, 2).forEach((s) => { T(c, s.name + ' · ' + STATES[s.state].long.toLowerCase() + ' dalle ' + hm(new Date(s.attnAt || m.sys.offAt || now())), x, y, mt.f3, { w: 500, max: w }); y += mt.f3 + 5; });
      if (last) {
        y += mt.pad;
        T(c, 'ULTIMO MESSAGGIO · ' + last.name, x, y, mt.f3 - 1, { w: 700, max: w }); y += mt.f3 + 4;
        wrapT(c, last.summary, x, y, mt.f2, w, 3, mt.f2 + 3, { w: 600 });
      }
      T(c, 'Schermo fermo per risparmiare batteria: si aggiorna quando il PC si riaccende', mt.pad * 2, H - mt.hint - mt.f3 - mt.pad, mt.f3, { w: 500, max: W - mt.pad * 4 });
      hints(c, dev, hintSet(dev, 'view'));
    }
    /* ---------- Tenuto 3 s (un tasto): indicatore mentre il tasto è giù ---------- */
    // m.ui.hold = { since, done, ok, next } dal keyDown al keyUp. Da 0,7 s un riquadro «TIENI PREMUTO» con
    // l'azione e una barra = tempo tenuto / 3 s, calcolata all'istante del disegno; il componente ridisegna
    // a 0,7, 1,4 e 2,1 s (un parziale della Heltec dura ~0,65 s: l'ultimo finisce prima dei 3 s). A 3 s
    // l'azione parte subito e resta «FATTO · RILASCIA» fino al rilascio, che non fa niente.
    const HOLD_SHOW = 700, HOLD_MS = 3000, HOLD_STEP = 700, HOLD_LAST = 2100;
    // Che cosa fa il tenuto 3 s qui, o null dove non ha effetto (la pressione lunga resta «indietro»).
    function holdLabel(m) {
      const tp = top(m);
      if (tp.id === 'boot' || tp.id === 'sleep' || tp.id === 'battlow' || tp.id === 'ota') return null;  // mai Riposo durante l'aggiornamento
      if (tp.id === 'calib' && calRunning(m)) return null;
      if (tp.id === 'confirm') return tp.until > now() ? (tp.kind === 'protect' ? 'PROTEGGI LE CHIAVI' : 'RIPRISTINO DI FABBRICA') : null;
      return 'RIPOSO';
    }
    function drawHold(c, dev, m) {
      const h = m.ui.hold;
      if (!h) return;
      const el = now() - h.since, label = h.done ? null : holdLabel(m);
      if (h.done ? !h.ok : (el < HOLD_SHOW || !label)) return;
      const k = dev.cls === 'S' ? 1 : Math.max(1, Math.round(dev.H / 160)), W = dev.W, H = dev.H;
      const bw = Math.min(W - 40 * k, 210 * k), bh = 46 * k, bx = Math.round((W - bw) / 2), by = Math.round((H - bh) / 2) - 4 * k;
      c.fillStyle = PAPER; c.fillRect(bx - 2 * k, by - 2 * k, bw + 4 * k, bh + 4 * k);
      c.fillStyle = INK; c.fillRect(bx, by, bw, bh);
      if (h.done) { T(c, 'FATTO · RILASCIA', W / 2, by + Math.round((bh - 14 * k) / 2), 14 * k, { w: 700, a: 'center', c: PAPER }); return; }
      T(c, 'TIENI PREMUTO', W / 2, by + 5 * k, 8 * k, { f: F_PIX, w: 700, a: 'center', c: PAPER });
      T(c, label, W / 2, by + 15 * k, 12 * k, { w: 700, a: 'center', c: PAPER, max: bw - 16 * k });
      box(c, bx + 8 * k, by + 32 * k, bw - 16 * k, 9 * k, PAPER, k);
      c.fillStyle = PAPER; c.fillRect(bx + 10 * k, by + 34 * k, Math.round((bw - 20 * k) * clamp(el / HOLD_MS, 0, 1)), 5 * k);
    }
    function draw(c, dev, m, f, hit, ctl) {
      drawScreen(c, dev, m, f, hit, ctl);
      drawHold(c, dev, m);
    }
    function drawScreen(c, dev, m, f, hit, ctl) {
      const tp = top(m), S = dev.cls === 'S';
      if (tp.id === 'boot') { boot(c, dev, m, f); return; }
      if (tp.id === 'sleep') { sleepScreen(c, dev, m, f); return; }
      if (tp.id === 'battlow') { battLowScreen(c, dev, m, f); return; }
      if (tp.id === 'calib') { calibScreen(c, dev, m, f); drawToast(c, dev, m); return; }
      if (tp.id === 'ota' && m.ota) { otaScreen(c, dev, m, f); drawToast(c, dev, m); return; }
      if (!m.sys.pc && (tp.id === 'home' || tp.id === 'detail')) { if (m.sys.pcOff) riepilogo(c, dev, m, f, hit, ctl); else offline(c, dev, m, f, hit, ctl); drawToast(c, dev, m); return; }
      if (tp.id === 'home') { if (!m.sessions.length) homeEmpty(c, dev, m, f, hit, ctl); else if (S) homeS(c, dev, m, f, hit, ctl); else if (dev.cls === 'M') homeM(c, dev, m, f, hit, ctl); else if (dev.cls === 'W') homeW(c, dev, m, f, hit, ctl); else homeL(c, dev, m, f, hit, ctl); }
      else if (tp.id === 'detail') { if (S) detailS(c, dev, m, f, hit, ctl); else detailBig(c, dev, m, f, hit, ctl); }
      else if (tp.id === 'perm') { if (S) permS(c, dev, m, f, hit, ctl); else permBig(c, dev, m, f, hit, ctl); }
      else if (tp.id === 'list') { const L = menuList(tp.menu, dev, m, tp.ssid); tp.sel = clamp(tp.sel || 0, 0, L.items.length - 1); listScreen(c, dev, m, f, hit, ctl, L.title, L.items, tp.sel); }
      else if (tp.id === 'wifiScan') { if (tp.scanUntil > now()) scanning(c, dev, m, f, hit, ctl); else { const items = netItems(m); tp.sel = clamp(tp.sel || 0, 0, items.length - 1); listScreen(c, dev, m, f, hit, ctl, 'Reti vicine', items, tp.sel); } }
      else if (tp.id === 'qr') qrScreen(c, dev, m, f, hit, ctl);
      else if (tp.id === 'connecting') connecting(c, dev, m, f, hit, ctl);
      else if (tp.id === 'oggi') oggi(c, dev, m, f, hit, ctl);
      else if (tp.id === 'storico') storico(c, dev, m, f, hit, ctl);
      else if (tp.id === 'batteria') batteria(c, dev, m, f, hit, ctl);
      else if (tp.id === 'stato') stato(c, dev, m, f, hit, ctl);
      else if (tp.id === 'limiti') limiti(c, dev, m, f, hit, ctl);
      else if (tp.id === 'focus') focusScreen(c, dev, m, f, hit, ctl);
      else if (tp.id === 'game') gameScreen(c, dev, m, f, hit, ctl);
      else if (tp.id === 'pet') petScreen(c, dev, m, f, hit, ctl);
      else if (tp.id === 'guest') guest(c, dev, m, f, hit, ctl);
      else if (tp.id === 'servizi') servizi(c, dev, m, f, hit, ctl);
      else if (tp.id === 'agenda') agenda(c, dev, m, f, hit, ctl);
      else if (tp.id === 'ambiente') ambiente(c, dev, m, f, hit, ctl);
      else if (tp.id === 'pair') pairS(c, dev, m, f, hit, ctl);
      else if (tp.id === 'confirm') confirmS(c, dev, m, f, hit, ctl);
      if (!S && attention(m) && !quiet(m) && tp.id !== 'home' && tp.id !== 'detail' && tp.id !== 'perm') banner(c, dev, m);
      drawToast(c, dev, m);
    }

    /* ---------- simulazione del pannello ---------- */
    class Screen {
      constructor(cv, dev) {
        this.cv = cv; this.dev = dev;
        cv.width = dev.W; cv.height = dev.H;
        this.out = cv.getContext('2d');
        this.src = document.createElement('canvas'); this.src.width = dev.W; this.src.height = dev.H;
        this.sc = this.src.getContext('2d', { willReadFrequently: true });
        this.n = dev.W * dev.H;
        this.cur = new Float32Array(this.n); this.ghost = new Float32Array(this.n);
        this.img = this.out.createImageData(dev.W, dev.H);
        this.partials = 0; this.flashing = false; this.hits = []; this.ghostOn = true;
      }
      compose(m, f, ctl) {
        const c = this.sc, dev = this.dev, hits = [];
        const hit = (x, y, w, h, fn) => { hits.push({ x: x, y: y, w: w, h: h, fn: fn }); };
        c.setTransform(1, 0, 0, 1, 0, 0);
        c.fillStyle = PAPER; c.fillRect(0, 0, dev.W, dev.H);
        try { draw(c, dev, m, f, hit, ctl); }
        catch (e) { c.fillStyle = PAPER; c.fillRect(0, 0, dev.W, dev.H); T(c, 'Errore di disegno', 6, 6, 12, { w: 700 }); T(c, String((e && e.message) || e), 6, 24, 10, { w: 500, max: dev.W - 12 }); }
        this.hits = hits;
        const px = c.getImageData(0, 0, dev.W, dev.H).data, t = new Float32Array(this.n);
        for (let i = 0, j = 0; i < this.n; i++, j += 4) {
          const l = (px[j] * 0.299 + px[j + 1] * 0.587 + px[j + 2] * 0.114) / 255;
          t[i] = dev.gray ? Math.round((1 - l) * 15) / 15 : (l < 0.5 ? 1 : 0);
        }
        if (m.set.invert) for (let i = 0; i < this.n; i++) t[i] = 1 - t[i];
        if (m.set.rot) t.reverse();
        return t;
      }
      show(m, f, ctl) { this.cur = this.compose(m, f, ctl); this.ghost.fill(0); this.paint(); }
      partial(m, f, ctl) {
        if (this.flashing) return;
        const t = this.compose(m, f, ctl), g = this.ghost, cur = this.cur;
        if (this.ghostOn) for (let i = 0; i < this.n; i++) if (cur[i] - t[i] > 0.4) g[i] = Math.min(0.24, g[i] + 0.06);
        this.cur = t; this.paint(); this.partials++;
        if (this.partials >= this.dev.fullEvery) this.full(m, f, ctl);
      }
      quick(m, f, ctl) {
        if (this.flashing) return;
        const t = this.compose(m, f, ctl);
        this.flashing = true;
        this.paint(t.map((x) => 1 - x));
        setTimeout(() => { this.cur = t; this.ghost.fill(0); this.paint(); this.flashing = false; this.partials++; }, 170);
      }
      full(m, f, ctl) {
        if (this.flashing) return;
        const t = this.compose(m, f, ctl);
        this.partials = 0; this.flashing = true;
        const inv = t.map((x) => 1 - x), ink = new Float32Array(this.n).fill(1), pap = new Float32Array(this.n), seq = [inv, ink, pap, inv];
        let k = 0;
        const step = () => {
          if (k < seq.length) { this.paint(seq[k++]); setTimeout(step, 190); return; }
          this.cur = t; this.ghost.fill(0); this.paint(); this.flashing = false;
        };
        step();
      }
      paint(arr) {
        const v = arr || this.cur, g = this.ghost, d = this.img.data, useG = !arr && this.ghostOn;
        const pr = PAPER_RGB[0], pg = PAPER_RGB[1], pb = PAPER_RGB[2], dr = INK_RGB[0] - pr, dg = INK_RGB[1] - pg, db = INK_RGB[2] - pb;
        for (let i = 0, j = 0; i < this.n; i++, j += 4) {
          let x = v[i];
          if (useG && g[i] > 0) x = x + g[i] * (1 - x);
          d[j] = pr + dr * x; d[j + 1] = pg + dg * x; d[j + 2] = pb + db * x; d[j + 3] = 255;
        }
        this.out.putImageData(this.img, 0, 0);
      }
    }

    return {
      DEVICES: DEVICES, STATES: STATES, CONTROLS: CONTROLS, HOME_LABEL: HOME_LABEL, ANIM_LABEL: ANIM_LABEL, SPEED_LABEL: SPEED_LABEL, NETS: NETS, SAVED_MAX: SAVED_MAX,
      Screen: Screen, newModel: newModel, count: count, logEv: logEv, toast: toast, isAttn: isAttn, attention: attention,
      urgentIdx: urgentIdx, sIdx: sIdx, canArchive: canArchive, unaIdx: unaIdx, muted: muted, powerSave: powerSave, top: top, addWait: addWait,
      rssiBars: rssiBars, menuList: menuList, netItems: netItems, animRate: animRate, frameFor: frameFor, profileText: profileText,
      quiet: quiet, focusOn: focusOn, permFromDisplay: permFromDisplay, permWaitText: permWaitText, permSince: permSince, defaultSet: defaultSet,
      holdLabel: holdLabel, HOLD_MS: HOLD_MS, HOLD_STEP: HOLD_STEP, HOLD_LAST: HOLD_LAST,
      pctFromMv: pctFromMv, mvFromPct: mvFromPct, calRate: calRate, calRunning: calRunning,
      CAL_PHASE_MS: CAL_PHASE_MS, CAL_EVERY_MS: CAL_EVERY_MS, CAL_MIN_PCT: CAL_MIN_PCT, OTA_RESULT_MS: OTA_RESULT_MS,
      WORK_DONE_MS: WORK_DONE_MS, WORK_PARTY_MS: WORK_PARTY_MS, minHm: minHm, dayAt: dayAt, nextMidnight: nextMidnight, sameDay: sameDay, hm: hm,
      workLeftText: workLeftText, workDone: workDone, RANKS: RANKS, GAME_SEED: GAME_SEED, gameNew: gameNew, gameGuess: gameGuess,
      AVATARS: AVATARS, AV_ORDER: AV_ORDER, avatarOf: avatarOf, avName: avName, drawAvatar: drawAvatar, parseAvatar: parseAvatar,
      ACCESSORIES: ACCESSORIES, ACC_ORDER: ACC_ORDER, ANCHORS: ANCHORS, parseAccessory: parseAccessory, parseAnchors: parseAnchors,
      drawAccessory: drawAccessory, avGrid: avGrid, accName: accName, petMsg: petMsg, applyGame: applyGame, petSwitch: petSwitch, levelCost: levelCost,
      PET_SIM_GOALS: PET_SIM_GOALS, PET_PARTY_MS: PET_PARTY_MS, PET_SPECIAL_TOAST: PET_SPECIAL_TOAST, petOn: petOn
    };
  }

  eng() {
    const C = this.constructor;
    if (!C._E) C._E = C.build();
    return C._E;
  }
  ensure() {
    if (!this.m) this.init();
  }
  init() {
    const E = this.eng();
    const id = E.DEVICES[this.props.device] ? this.props.device : 'heltec213';
    this.dev = E.DEVICES[id];
    this.m = E.newModel(this.dev);
    this.frame = 0; this.lastFrame = 0; this.count = 0; this.cTimer = null;
    this.downAt = {}; this.pressed = {}; this.lastPtr = {};
    this.lastInput = 'Nessun comando';
    this.lastAction = 'Premi i tasti del dispositivo qui a sinistra.';
    this.auto = true; this.touch = false; this.ledOn = false; this.beepUntil = 0;
    this.lastInputAt = 0; this.pendingKind = ''; this.permText = ''; this.lastMin = -1; this.lastPage = 0;
    if (!this.uid) this.uid = 'cal' + Math.random().toString(36).slice(2, 9);
    if (!this.handlersReady) this.makeHandlers();
  }
  makeHandlers() {
    this.handlersReady = true;
    this.setCanvas = (el) => {
      if (!el) return;
      if (el === this.cv && this.scr) return;
      this.ensure();
      this.cv = el;
      this.scr = new (this.eng().Screen)(el, this.dev);
      this.scr.ghostOn = this.props.ghosting !== false;
      this.scr.show(this.m, this.fr(), this.ctl);
    };
    this.tap = (e) => this.onTap(e);
    this.noCtx = (e) => { if (e && e.preventDefault) e.preventDefault(); };
    this.kh = {};
    ['single', 'up', 'down', 'ok', 'menu', 'exit', 'left', 'mid', 'right'].forEach((id) => {
      this.kh[id] = {
        down: (e) => { if (e && e.button !== undefined && e.button !== 0) return; this.keyDown(id); },
        up: () => this.keyUp(id),
        leave: () => { if (this.pressed[id]) this.keyUp(id); },
        kd: (e) => { if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) { e.preventDefault(); this.keyDown(id); } },
        ku: (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); this.keyUp(id); } },
        click: () => { if (Date.now() - (this.lastPtr[id] || 0) < 1500) return; this.keyDown(id); this.keyUp(id); }
      };
    });
    this.ctl = {
      back: () => this.act('back'),
      ok: () => this.act('ok'),
      openSession: (i) => { this.m.ui.sel = i; this.m.ui.selUntil = Date.now() + 15000; return this.act('ok'); },
      listTap: (i) => { const tp = this.eng().top(this.m); tp.sel = i; return this.act('ok'); },
      permTap: (k) => {
        const tp = this.eng().top(this.m);
        if (tp.choice === k) return this.act('ok');
        tp.choice = k; this.redraw('partial');
        return 'Scelta: ' + (k === 0 ? 'CONSENTI' : 'NEGA');
      }
    };
    this.simClicks = {
      perm: () => this.simEvent('permission'),
      input: () => this.simEvent('input'),
      error: () => this.simEvent('error'),
      auto: () => { this.auto = !this.auto; this.lastInput = 'Simula'; this.lastAction = 'Sessioni automatiche: ' + (this.auto ? 'sì' : 'no'); this.forceUpdate(); },
      pc: () => {
        const s = this.m.sys;
        s.pc = !s.pc; if (!s.pc) s.lastPc = Date.now(); else s.pcOff = false;
        this.eng().toast(this.m, s.pc ? 'PC di nuovo raggiungibile' : 'PC non raggiungibile');
        this.lastInput = 'Simula'; this.lastAction = s.pc ? 'Il servizio sul PC risponde di nuovo' : 'Servizio sul PC spento';
        this.redraw('quick'); this.forceUpdate();
      },
      wifi: () => {
        const s = this.m.sys;
        s.wifi = !s.wifi; s.pc = s.wifi; if (!s.wifi) s.lastPc = Date.now();
        this.eng().toast(this.m, s.wifi ? 'Wi-Fi di nuovo connesso' : 'Wi-Fi assente');
        this.lastInput = 'Simula'; this.lastAction = s.wifi ? 'Wi-Fi tornato' : 'Wi-Fi assente: anche il PC non è raggiungibile';
        this.redraw('quick'); this.forceUpdate();
      },
      usb: () => {
        const s = this.m.sys;
        s.usb = !s.usb;
        this.eng().toast(this.m, s.usb ? 'In carica' : 'A batteria');
        this.lastInput = 'Simula'; this.lastAction = s.usb ? 'Cavo USB-C collegato' : 'Cavo USB-C scollegato';
        this.redraw('partial'); this.forceUpdate();
      },
      battlvl: () => {
        const s = this.m.sys, L = [100, 50, 10];
        s.batt = L[(L.indexOf(s.batt) + 1) % L.length];
        if (s.batt < 15 && !s.usb) this.eng().toast(this.m, 'Batteria bassa · risparmio attivo');
        this.lastInput = 'Simula'; this.lastAction = 'Batteria al ' + s.batt + '%';
        this.redraw('partial'); this.forceUpdate();
      },
      batt: () => {
        const s = this.m.sys;
        s.batt = Math.max(3, s.batt - 20);
        if (s.batt < 15 && !s.usb) this.eng().toast(this.m, 'Batteria bassa · risparmio attivo');
        this.lastInput = 'Simula'; this.lastAction = 'Batteria al ' + Math.round(s.batt) + '%';
        this.redraw('partial'); this.forceUpdate();
      },
      pcoff: () => {
        const s = this.m.sys, E = this.eng();
        if (!s.pcOff) {
          s.pcOff = true; s.pc = false; s.offAt = Date.now(); s.lastPc = s.offAt;
          this.m.ui.stack = [{ id: 'home' }];
          E.logEv(this.m, null, 'PC spento');
          this.lastAction = 'PC spento: il display mostra il riepilogo e resta fermo';
        } else {
          s.pcOff = false; s.pc = true;
          E.toast(this.m, 'PC acceso');
          E.logEv(this.m, null, 'PC acceso');
          this.lastAction = 'PC di nuovo acceso';
        }
        this.lastInput = 'Simula'; this.redraw('quick'); this.forceUpdate();
      },
      // M6: batteria sotto 3,4 V senza cavo: schermata «Batteria scarica» e sonno profondo senza timer.
      battcrit: () => {
        const m = this.m, s = m.sys, E = this.eng();
        s.batt = 3; s.usb = false;
        m.calib = null;
        m.ui.stack = [{ id: 'home' }, { id: 'battlow' }];
        E.logEv(m, null, 'batteria scarica: sonno profondo');
        this.lastInput = 'Simula'; this.lastAction = 'Batteria scarica: il display si spegne finché non colleghi la USB-C';
        this.redraw('quick'); this.forceUpdate();
      },
      // 0.10.1: ora non ancora nota (accensione a freddo senza welcome né SNTP): «--:--» e niente data. Sulla scheda arriva
      // dal PC o da pool.ntp.org appena c'è il Wi-Fi.
      noclock: () => {
        const s = this.m.sys;
        s.wallOk = s.wallOk === false;
        this.lastInput = 'Simula'; this.lastAction = s.wallOk ? 'Ora vera arrivata (welcome o SNTP)' : 'Ora non ancora nota: l\'orologio mostra «--:--»';
        this.redraw('partial'); this.forceUpdate();
      },
      // M6: una calibrazione già salvata (8 %/h in Attivo, 1,5 %/h in Attesa), per vedere l'autonomia senza aspettare 2 ore.
      // Aggiornamento dal PC, a giro: 0% → 37% → 80% → verifica → riavvio → errore → rifiutato → chiuso.
      // Ogni passo è un ridisegno: la corsa avanza di un fotogramma (sulla scheda: ogni ~5% o 2 s, docs/OTA.md).
      ota: () => {
        const m = this.m, t = Date.now(), total = 1556033;
        const seq = m.ota ? m.ota.seq + 1 : 0;
        if (seq > 6) {
          if (this.eng().top(m).id === 'ota') m.ui.stack.pop();
          m.ota = null;
          this.lastInput = 'Simula'; this.lastAction = 'Aggiornamento: schermata chiusa';
          this.redraw('quick'); this.forceUpdate(); return;
        }
        if (!m.ota) { m.ota = { seq: 0, from: '0.6.0', to: '0.7.0', total: total, done: 0, step: 0, why: '', until: 0 }; m.ui.stack.push({ id: 'ota' }); }
        const o = m.ota;
        o.seq = seq; o.step = seq; o.until = 0; o.why = '';
        const S = [['receive', 0], ['receive', 0.37], ['receive', 0.8], ['verify', 1], ['reboot', 1], ['error', 0.62], ['rejected', 0]][seq];
        o.phase = S[0]; o.done = Math.ceil(total * S[1]);
        if (o.phase === 'error') { o.why = 'download interrotto: nessun dato da 15 s'; o.until = t + this.eng().OTA_RESULT_MS; }
        if (o.phase === 'rejected') { o.why = 'batteria al 42%: collega il cavo USB o ricarica fino al 50%'; o.until = t + this.eng().OTA_RESULT_MS; }
        const L = { receive: 'ricezione ' + Math.floor(o.done * 100 / total) + '%', verify: 'verifica', reboot: 'fatto, riavvio', error: 'errore', rejected: 'rifiutato' };
        this.lastInput = 'Simula'; this.lastAction = 'Aggiornamento 0.6.0 → 0.7.0: ' + L[o.phase];
        this.redraw(seq === 0 ? 'quick' : 'partial'); this.forceUpdate();
      },
      // Giornata di lavoro (0.9.0): come il comando `workday` del PC (start, poi stop). Sulla scheda arriva solo dal PC
      // autenticato e risponde con un ack; qui il pannello «Ultimo comando» dice l'esito.
      work: () => {
        const r = this.m.work.on ? this.workStop() : this.workStart();
        this.lastInput = 'Simula'; this.lastAction = 'Dal PC: ' + r + (this.workErr ? ' (ack: ' + this.workErr + ')' : '');
        this.redraw('partial'); this.forceUpdate();
      },
      // Ultimi minuti: la giornata di oggi finisce fra 25 minuti (la accende, se serve). Solo nel simulatore.
      worklast: () => {
        const m = this.m, t = Date.now();
        if (!m.work.on) { m.work.on = true; m.work.from = t; m.work.doneUntil = 0; }
        m.work.endAt = t + 25 * 60000;
        this.lastInput = 'Simula'; this.lastAction = 'Giornata di lavoro: fine fra 25 minuti';
        this.redraw('partial'); this.forceUpdate();
      },
      // Salta alla fine: la giornata finisce adesso (al prossimo tick: «GIORNATA FINITA» e la festa).
      workend: () => {
        const m = this.m, t = Date.now();
        if (!m.work.on) { m.work.on = true; m.work.from = t; m.work.doneUntil = 0; }
        m.work.endAt = t;
        this.lastInput = 'Simula'; this.lastAction = 'Giornata di lavoro: salta alla fine';
        this.forceUpdate();
      },
      // Gioco: torna al seme fisso (stessa sequenza di carte dei test) e, se si sta giocando, ricomincia la partita.
      gameseed: () => {
        const m = this.m, E = this.eng();
        m.game.seed = E.GAME_SEED;
        if (E.top(m).id === 'game') { E.gameNew(m); this.redraw('partial'); }
        this.lastInput = 'Simula'; this.lastAction = 'Gioco: seme fisso ' + E.GAME_SEED;
        this.forceUpdate();
      },
      // Gioco della mascotte: i pulsanti fanno le veci del servizio (messaggio `game`, impostazione remota `game`).
      // Acceso o spento dal PC (webapp, calamaio game on|off): il servizio manda `game` con on, la scheda si allinea.
      game: () => {
        const m = this.m, E = this.eng(), g = E.petMsg(m.pet);
        g.on = !m.set.game;
        E.applyGame(m, g);
        this.lastInput = 'Simula'; this.lastAction = 'Dal PC: gioco della mascotte ' + (m.set.game ? 'acceso' : 'spento');
        this.redraw('partial'); this.forceUpdate();
      },
      // Sale di livello: XP all'inizio del nuovo livello + 25; ai livelli pari si sblocca il primo accessorio del catalogo
      // non ancora sbloccato.
      petlevel: () => {
        const m = this.m, E = this.eng(), g = E.petMsg(m.pet);
        g.level = m.pet.level + 1; g.xpLevelStart = m.pet.xpNext; g.xpNext = m.pet.xpNext + E.levelCost(g.level); g.xp = g.xpLevelStart + 25;
        let un = '';
        if (g.level % 2 === 0) {
          un = E.ACC_ORDER.find((id) => !g.unlocked.some((u) => u.id === id)) || '';
          if (un) g.unlocked.push({ id: un, name: E.accName(un) });
        }
        E.applyGame(m, g);
        this.lastInput = 'Simula'; this.lastAction = 'Gioco: livello ' + g.level + (un ? ', sbloccato ' + E.accName(un) : '');
        this.redraw('partial'); this.forceUpdate();
      },
      // Traguardo (celebrate): i primi della lista del servizio, a giro.
      petgoal: () => {
        const m = this.m, E = this.eng(), L = E.PET_SIM_GOALS, q = L[(this.goalSeq || 0) % L.length], g = E.petMsg(m.pet);
        this.goalSeq = (this.goalSeq || 0) + 1;
        g.celebrate = { id: q[0], title: q[1] };
        const r = E.applyGame(m, g);
        this.lastInput = 'Simula';
        this.lastAction = 'Traguardo: ' + q[1] + (r === 'festa' ? ' (festa di 3 s)' : r === 'toast' ? ' (solo il toast)' : m.set.game && m.pet.on ? ' (già festeggiato)' : ' (gioco spento: niente)');
        this.redraw('partial'); this.forceUpdate();
      },
      // Indossa (come dalla webapp): nessuno → gli accessori del catalogo in ordine → nessuno; se serve lo sblocca.
      petwear: () => {
        const m = this.m, E = this.eng(), g = E.petMsg(m.pet), L = [''].concat(E.ACC_ORDER);
        const next = L[(L.indexOf(g.wear ? g.wear.id : '') + 1) % L.length];
        g.wear = next ? { id: next, crc: E.ACCESSORIES[next].crc } : null;
        if (next && !g.unlocked.some((u) => u.id === next)) g.unlocked.push({ id: next, name: E.accName(next) });
        E.applyGame(m, g);
        this.lastInput = 'Simula'; this.lastAction = 'Dal PC: ' + (next ? 'indossa ' + E.accName(next) : 'nessun accessorio');
        this.redraw('partial'); this.forceUpdate();
      },
      // Accessorio speciale disponibile (catalogo esaurito e un traguardo nuovo): special a giro.
      petspecial: () => {
        const m = this.m, E = this.eng(), g = E.petMsg(m.pet);
        g.special = !g.special;
        E.applyGame(m, g);
        this.lastInput = 'Simula'; this.lastAction = 'Gioco: accessorio speciale ' + (g.special ? 'disponibile' : 'chiesto');
        this.redraw('partial'); this.forceUpdate();
      },
      // Stanca (3 ore senza pausa saggia, energia 15%) o riposata (80%).
      pettired: () => {
        const m = this.m, E = this.eng(), g = E.petMsg(m.pet);
        g.tired = !g.tired; g.energy = g.tired ? 15 : 80;
        E.applyGame(m, g);
        this.lastInput = 'Simula'; this.lastAction = 'Gioco: mascotte ' + (g.tired ? 'stanca' : 'riposata');
        this.redraw('partial'); this.forceUpdate();
      },
      calsaved: () => {
        const m = this.m;
        m.bat.cal = m.bat.cal ? null : { active: 8, idle: 1.5, at: Date.now() };
        this.lastInput = 'Simula'; this.lastAction = m.bat.cal ? 'Batteria calibrata: 8 %/h in Attivo, 1,5 %/h in Attesa' : 'Calibrazione cancellata';
        this.redraw('partial'); this.forceUpdate();
      },
      limit: () => {
        if (this.m.limits.h5 == null) this.m.limits.h5Reset = Date.now() + 130 * 60000;
        this.m.limits.h5 = 92;
        this.eng().toast(this.m, 'Limite 5 ore al 92%');
        this.lastInput = 'Simula'; this.lastAction = 'Limite delle 5 ore al 92%: Goccia è stanca';
        this.changed(); this.forceUpdate();
      },
      acc: () => {
        const m = this.m;
        m.acc = !m.acc; if (m.acc) m.air.co2 = 820;
        this.eng().toast(m, m.acc ? 'Accessori collegati' : 'Accessori scollegati');
        this.lastInput = 'Simula'; this.lastAction = m.acc ? 'Sensore CO2 e buzzer collegati ai pin' : 'Accessori scollegati';
        this.redraw('partial'); this.forceUpdate();
      },
      air: () => {
        const m = this.m;
        m.acc = true; m.air.co2 = 1250;
        this.eng().toast(m, 'CO2 alta: apri la finestra');
        this.lastInput = 'Simula'; this.lastAction = 'CO2 a 1250 ppm: Goccia boccheggia';
        this.changed(); this.forceUpdate();
      },
      // M3: stati che col firmware arrivano dalla rete vera.
      empty: () => {
        const m = this.m, E = this.eng();
        if (m.sessions.length) {
          this.sesBak = m.sessions; m.sessions = [];
          while (E.top(m).id === 'detail' || E.top(m).id === 'perm') m.ui.stack.pop();
          m.ui.sel = 0; m.ui.selUntil = 0; m.ui.page = 0;
          this.lastAction = 'Nessuna sessione aperta sul PC';
        } else {
          m.sessions = this.sesBak || [];
          this.lastAction = 'Sessioni di nuovo aperte';
        }
        this.lastInput = 'Simula'; this.redraw('quick'); this.forceUpdate();
      },
      nolimits: () => {
        const m = this.m;
        if (m.limits.h5 != null || m.limits.d7 != null) {
          this.limBak = Object.assign({}, m.limits);
          m.limits = { h5: null, h5Reset: null, d7: null, d7Reset: null };
          this.lastAction = 'Limiti: nessun dato dal PC';
        } else {
          m.limits = this.limBak;
          this.lastAction = 'Limiti di nuovo dal PC';
        }
        this.lastInput = 'Simula'; this.redraw('partial'); this.forceUpdate();
      },
      joinfail: () => {
        const m = this.m, E = this.eng(), tp = E.top(m);
        this.lastInput = 'Simula';
        if (tp.id !== 'connecting') { this.lastAction = 'Nessuna connessione in corso'; this.forceUpdate(); return; }
        const ssid = tp.ssid;
        m.sys.wifi = false;
        E.logEv(m, null, 'Wi-Fi: ' + ssid + ' non risponde');
        m.ui.stack.pop();  // torna al QR o all'elenco delle reti
        E.toast(m, 'Connessione non riuscita');
        this.lastAction = 'Connessione a ' + ssid + ' non riuscita';
        this.redraw('quick'); this.forceUpdate();
      },
      elsewhere: () => {
        const m = this.m, E = this.eng(), tp = E.top(m);
        this.lastInput = 'Simula';
        const i = tp.id === 'perm' ? tp.i : m.sessions.findIndex((x) => x.state === 'permission');
        if (i < 0) { this.lastAction = 'Nessun permesso in attesa'; this.forceUpdate(); return; }
        const s = m.sessions[i];
        s.state = 'working'; s._t = 0;
        m.stats.permTerminal++;
        E.addWait(m, s);
        E.logEv(m, s, 'risposto dal terminale');
        if (tp.id === 'perm') { m.ui.stack.pop(); E.toast(m, 'Risposto altrove'); this.redraw('quick'); }
        else this.changed();
        this.lastAction = 'Permesso di ' + s.name + ' risposto nel terminale';
        this.forceUpdate();
      },
      prompt: () => {
        const m = this.m, E = this.eng();
        const s = m.archived[0] || m.sessions.find((x) => x.state === 'done' || x.state === 'idle');
        this.lastInput = 'Simula';
        if (!s) { this.lastAction = 'Nessuna sessione ferma o archiviata'; this.forceUpdate(); return; }
        this.revive(s);
        s.tasks = [['Analizza il codice', 1], ['Implementa', 0], ['Verifica finale', 0]];
        E.count(s);
        s.state = 'working'; s.detail = 'Read package.json'; s.since = Date.now(); s._t = 0; s.summary = ''; s.test = null;
        E.logEv(m, s, 'nuovo prompt, 3 task');
        this.lastAction = 'Nuovo prompt a ' + s.name;
        this.changed(); this.forceUpdate();
      },
      // M5: abbinamento dal PC (calamaio pair). Il codice è quello di protocol/vectors/crypto-vectors.json.
      pair: () => {
        const m = this.m, E = this.eng();
        this.lastInput = 'Simula';
        if (E.top(m).id === 'sleep') m.ui.stack.pop();
        const tp = E.top(m);
        if (tp.id === 'pair') { tp.code = '175397'; tp.sel = 0; tp.until = Date.now() + 180000; this.redraw('partial'); }
        else { m.ui.stack.push({ id: 'pair', code: '175397', sel: 0, until: Date.now() + 180000 }); this.redraw('quick'); }
        this.lastAction = 'Abbinamento: scrivi il codice sul PC';
        this.forceUpdate();
      },
      pairok: () => {
        const m = this.m, tp = this.eng().top(m);
        this.lastInput = 'Simula';
        if (tp.id !== 'pair') { this.lastAction = 'Nessun abbinamento in corso'; this.forceUpdate(); return; }
        tp.sel = 1; this.redraw('partial');
        this.lastAction = 'Il PC ha il codice giusto: conferma con 2 pressioni';
        this.forceUpdate();
      },
      pairbad: () => {
        const m = this.m, E = this.eng(), tp = E.top(m);
        this.lastInput = 'Simula';
        if (tp.id !== 'pair') { this.lastAction = 'Nessun abbinamento in corso'; this.forceUpdate(); return; }
        m.ui.stack.pop(); E.toast(m, 'Abbinamento annullato'); this.redraw('quick');
        this.lastAction = 'Abbinamento annullato dal PC (codice)';
        this.forceUpdate();
      },
      redo: () => {
        const y = this.m.sys;
        y.pair = y.pair === 'redo' ? 'paired' : 'redo';
        if (y.pair === 'paired' && !y.pairHost) y.pairHost = 'calamaio.local';
        this.lastInput = 'Simula';
        this.lastAction = y.pair === 'redo' ? 'Abbinamento da rifare: le chiavi del display e del PC non coincidono' : 'Abbinamento di nuovo valido';
        this.redraw('partial'); this.forceUpdate();
      },
      // Avatar (docs/AVATAR.md): sul display li decide il servizio (state.sessions[].avatar, defaultAvatar).
      // Avatar della sessione mostrata (Dettaglio, Permesso, altrimenti quella della Home): predefinito → gli
      // avatar della libreria tranne la Goccia → di nuovo predefinito.
      avatar: () => {
        const m = this.m, E = this.eng(), tp = E.top(m);
        this.lastInput = 'Simula';
        if (!m.sessions.length) { this.lastAction = 'Nessuna sessione'; this.forceUpdate(); return; }
        const i = tp.id === 'detail' ? m.ui.sel : tp.id === 'perm' ? tp.i : m.set.homeStyle === 'una' ? E.unaIdx(m) : E.sIdx(m);
        const s = m.sessions[i], L = [''].concat(E.AV_ORDER.filter((id) => id !== 'goccia'));
        s.avatar = L[(L.indexOf(s.avatar || '') + 1) % L.length];
        this.lastAction = 'Avatar di ' + s.name + ': ' + (s.avatar ? E.avName(s.avatar) : 'predefinito');
        this.redraw('partial'); this.forceUpdate();
      },
      // Avatar predefinito (welcome e state del servizio): Goccia → Gatto → Demone → Calamaro → Goccia.
      defavatar: () => {
        const m = this.m, E = this.eng(), L = E.AV_ORDER.length ? E.AV_ORDER : ['goccia'];
        m.set.defaultAvatar = L[(L.indexOf(m.set.defaultAvatar) + 1) % L.length];
        this.lastInput = 'Simula';
        this.lastAction = 'Avatar predefinito: ' + E.avName(m.set.defaultAvatar);
        this.redraw('partial'); this.forceUpdate();
      },
      touch: () => { this.touch = !this.touch; this.lastInput = 'Simula'; this.lastAction = this.touch ? 'Tocco attivo: tocca lo schermo' : 'Tocco spento'; this.forceUpdate(); },
      full: () => { if (this.scr) this.scr.full(this.m, this.fr(), this.ctl); this.lastInput = 'Simula'; this.lastAction = 'Refresh completo del pannello'; this.forceUpdate(); }
    };
  }

  componentDidMount() {
    this.ensure();
    this.iv = setInterval(() => this.tick(), 100);
    this.simIv = setInterval(() => { if (this.auto && !document.hidden) this.step(); }, 4200);
    this.slowIv = setInterval(() => this.slowTick(), 10000);
    this.findCv = setTimeout(() => {
      if (!this.scr) {
        const el = document.querySelector('canvas[data-cal="' + this.uid + '"]');
        if (el) this.setCanvas(el);
      }
    }, 80);
    try {
      const fl = ['700 16px "Chakra Petch"', '600 16px "Chakra Petch"', '500 16px "Chakra Petch"', '400 8px "Silkscreen"', '700 8px "Silkscreen"'];
      Promise.all(fl.map((x) => document.fonts.load(x))).then(() => { if (this.scr) this.scr.show(this.m, this.fr(), this.ctl); }, () => {});
    } catch (e) {
      /* fonts API non disponibile */
    }
  }
  componentWillUnmount() {
    clearInterval(this.iv); clearInterval(this.simIv); clearInterval(this.slowIv);
    clearTimeout(this.cTimer); clearTimeout(this.pending); clearTimeout(this.findCv);
  }
  componentDidUpdate(prevProps) {
    const a = (prevProps && prevProps.device) || 'heltec213', b = this.props.device || 'heltec213';
    if (a !== b) {
      const cv = this.cv;
      this.scr = null; this.cv = null; this.m = null;
      this.init();
      if (cv && cv.isConnected) this.setCanvas(cv);
      this.forceUpdate();
      return;
    }
    if (this.scr) this.scr.ghostOn = this.props.ghosting !== false;
  }

  fr() {
    return this.eng().frameFor(this.dev, this.m, this.frame, Date.now());
  }
  redraw(kind) {
    if (!this.scr) return;
    if (this.pending) { clearTimeout(this.pending); if (this.pendingKind === 'quick') kind = 'quick'; }
    this.pendingKind = kind;
    this.pending = setTimeout(() => {
      this.pending = null;
      const k = this.pendingKind; this.pendingKind = '';
      if (!this.scr) return;
      if (k === 'quick') this.scr.quick(this.m, this.fr(), this.ctl); else this.scr.partial(this.m, this.fr(), this.ctl);
      this.forceUpdate();
    }, 140);
  }
  tick() {
    const E = this.eng(), m = this.m;
    if (!m || !this.scr || document.hidden) return;
    const t = Date.now(), tp = E.top(m);
    const h = m.ui.hold;
    if (h && !h.done && t >= h.next && h.next - h.since <= E.HOLD_LAST) {
      if (E.holdLabel(m)) this.redraw('partial');  // indicatore del tenuto: 0,7 · 1,4 · 2,1 s
      h.next += E.HOLD_STEP;
    }
    if (tp.id === 'boot' && t > tp.until) { m.ui.stack = [{ id: 'home' }]; this.redraw('quick'); return; }
    if (tp.id === 'pair' && tp.until && t > tp.until) {
      // La finestra di 3 minuti (M5 §1): sul firmware con la rete la chiude il servizio.
      m.ui.stack.pop();
      E.toast(m, 'Abbinamento annullato');
      this.lastAction = 'Abbinamento scaduto: 3 minuti senza conferma';
      this.redraw('quick'); return;
    }
    if (tp.id === 'connecting' && t > tp.until) { this.finishConnect(tp); return; }
    if (tp.id === 'ota' && m.ota && m.ota.until && t > m.ota.until) { m.ui.stack.pop(); m.ota = null; this.redraw('quick'); return; }
    if (tp.id === 'wifiScan' && tp.scanUntil && t > tp.scanUntil) { tp.scanUntil = 0; this.redraw('partial'); }
    if (m.toast.until && t > m.toast.until) { m.toast.until = 0; m.toast.msg = ''; this.redraw('partial'); }
    if (m.ui.selUntil && t > m.ui.selUntil) { m.ui.selUntil = 0; if (this.dev.cls === 'S') this.redraw('partial'); }
    if (m.set.mute && t > m.set.mute) { m.set.mute = 0; m.set.muteKind = ''; this.redraw('partial'); }
    if (m.focus.until && t > m.focus.until) {
      m.focus.until = 0; m.burstUntil = t + 4000;
      E.toast(m, 'Fine concentrazione · fai una pausa', 5000);
      E.logEv(m, null, 'concentrazione finita');
      this.lastAction = 'Fine dei 25 minuti di concentrazione';
      this.redraw('partial');
    }
    this.workTick(t);
    if (m.pet.party && t >= m.pet.party) { m.pet.party = 0; this.redraw('partial'); }
    if (m.pet.specialToast && !m.toast.msg) { m.pet.specialToast = false; if (E.petOn(m)) { E.toast(m, E.PET_SPECIAL_TOAST, 5000); this.redraw('partial'); } }
    if (m.limits.h5 != null && t > m.limits.h5Reset) { m.limits.h5 = 3; m.limits.h5Reset = t + 5 * 3600000; this.redraw('partial'); }
    if (m.limits.d7 != null && t > m.limits.d7Reset) { m.limits.d7 = 1; m.limits.d7Reset = t + 7 * 24 * 3600000; }
    if (m.ui.forgetUntil && t > m.ui.forgetUntil) { m.ui.forgetUntil = 0; this.redraw('partial'); }
    if (m.ui.forgetNetUntil && t > m.ui.forgetNetUntil) { m.ui.forgetNetUntil = 0; this.redraw('partial'); }
    if (m.ui.unpairUntil && t > m.ui.unpairUntil) { m.ui.unpairUntil = 0; this.redraw('partial'); }
    if (m.ui.newBattUntil && t > m.ui.newBattUntil) { m.ui.newBattUntil = 0; this.redraw('partial'); }
    this.calibTick(t);
    if (tp.id === 'confirm' && tp.until && t > tp.until) { tp.until = 0; this.redraw('partial'); }
    if (t > m.nextBurst) { m.burstUntil = t + 2500; m.nextBurst = t + 30000; }
    if (tp.id === 'perm') {
      // Nessuna scadenza: «IN ATTESA DA …» si ridisegna solo quando cambia (al più una volta al minuto).
      const w = E.permWaitText(E.permSince(m, tp));
      if (w !== this.permText) { this.permText = w; this.redraw('partial'); }
    }
    const mm = new Date().getMinutes();
    // L'orologio: ogni minuto; con il risparmio energetico ogni 5 minuti.
    if (mm !== this.lastMin) { if (this.lastMin !== -1 && (!E.powerSave(m) || mm % 5 === 0)) this.redraw('partial'); this.lastMin = mm; }
    if (this.dev.cls === 'S' && m.set.homeStyle === 'una' && tp.id === 'home' && !E.attention(m) && !(m.ui.selUntil > t) && t - this.lastPage > 8000 && m.sessions.length > 0 && !E.powerSave(m)) {
      this.lastPage = t; m.ui.page = (m.ui.page + 1) % m.sessions.length; this.redraw('partial');
    }
    const rate = E.animRate(this.dev, m, t);
    if (rate > 0 && !this.pending && !this.scr.flashing && t - this.lastFrame >= 1000 / rate) {
      this.lastFrame = t; this.frame++;
      this.scr.partial(m, this.fr(), this.ctl);
      this.forceUpdate();
    }
    if (this.dev.led) {
      const on = m.set.led && E.attention(m) && !E.quiet(m) && Math.floor(t / 500) % 2 === 0;
      if (on !== this.ledOn) { this.ledOn = on; this.forceUpdate(); }
    }
    if (this.beepUntil && t > this.beepUntil) { this.beepUntil = 0; this.forceUpdate(); }
  }
  // M6: avanzamento della calibrazione (sul firmware: Ui::tick, e la lettura vera della tensione dal loop).
  // Il tempo con il cavo collegato non conta (pausa). Una lettura al minuto, la prima subito.
  calibTick(t) {
    const E = this.eng(), m = this.m, k = m.calib;
    if (!k || k.phase === 'done') return;
    const dt = t - k.last;
    k.last = t;
    if (k.phase === 'wait') {
      if (m.sys.usb) return;
      if (m.sys.batt < E.CAL_MIN_PCT) { this.calibAbort('Carica almeno al 90%'); return; }
      k.phase = 'max'; k.ms = 0; k.next = 0; k.samples = []; k.paused = false;
      E.logEv(m, null, 'calibrazione: fase 1');
      this.redraw('partial');
    }
    if (m.sys.usb) { if (!k.paused) { k.paused = true; this.redraw('partial'); } return; }
    if (k.paused) { k.paused = false; this.redraw('partial'); }
    else k.ms += dt;
    // Simulatore: la batteria scende come su una scheda vera (8 %/h in fase 1, 1,5 %/h in fase 2).
    m.sys.batt = Math.max(0, m.sys.batt - dt * (k.phase === 'max' ? 8 : 1.5) / 3600000);
    if (k.ms >= k.next) this.calibSample(E.mvFromPct(m.sys.batt), t);
  }
  calibSample(mv, t) {
    const E = this.eng(), m = this.m, k = m.calib;
    k.mv = mv;
    k.samples.push([k.ms / 3600000, E.pctFromMv(mv)]);
    k.next += E.CAL_EVERY_MS;
    if (k.ms >= E.CAL_PHASE_MS) {
      const r = E.calRate(k.samples);
      if (k.phase === 'max') {
        k.rMax = r; k.phase = 'idle'; k.ms = 0; k.next = 0; k.samples = [];
        E.logEv(m, null, 'calibrazione: fase 2');
      } else {
        if (k.rMax == null || r == null || k.rMax <= 0 || r <= 0) { this.calibAbort('Calibrazione non riuscita'); return; }
        k.phase = 'done';
        m.bat.cal = { active: k.rMax, idle: r, at: t };
        E.logEv(m, null, 'batteria calibrata');
        E.toast(m, 'Batteria calibrata');
      }
    }
    this.redraw('partial');
  }
  calibAbort(msg) {
    const m = this.m, E = this.eng();
    m.calib = null;
    while (m.ui.stack.length > 1 && E.top(m).id === 'calib') m.ui.stack.pop();
    E.toast(m, msg);
    E.logEv(m, null, 'calibrazione annullata');
    this.lastAction = msg;
    this.redraw('quick');
  }
  slowTick() {
    const m = this.m;
    if (!m || document.hidden) return;
    const E = this.eng(), s = m.sys;
    const sig = () => Math.round(s.batt) + '|' + E.rssiBars(s.rssi) + '|' + s.temp.toFixed(1);
    const before = sig();
    if (s.usb) s.batt = Math.min(100, s.batt + 1); else s.batt = Math.max(1, s.batt - 0.15);
    s.rssi = Math.max(-80, Math.min(-50, s.rssi + Math.round((Math.random() - 0.5) * 6)));
    s.temp = Math.round((s.temp + (Math.random() - 0.5) * 0.2) * 10) / 10;
    if (before !== sig() && this.dev.cls !== 'S') this.redraw('partial');
  }
  finishConnect(tp) {
    const E = this.eng(), m = this.m, net = E.NETS.find((n) => n.ssid === tp.ssid);
    m.sys.wifi = true; m.sys.pc = true; m.sys.ssid = tp.ssid; m.sys.rssi = net ? net.rssi : -60;
    // La rete scelta si aggiunge alle salvate (se c'è posto: con 5 reti il portale chiede quale togliere).
    const sv = m.saved.find((x) => x.ssid === tp.ssid);
    if (sv) sv.used = Date.now();
    else if (m.saved.length < E.SAVED_MAX) m.saved.push({ ssid: tp.ssid, used: Date.now() });
    while (m.ui.stack.length > 1 && E.top(m).id !== 'list') m.ui.stack.pop();
    E.toast(m, 'Connesso a ' + tp.ssid);
    E.logEv(m, null, 'Wi-Fi connesso a ' + tp.ssid);
    this.lastAction = 'Connesso a ' + tp.ssid;
    this.redraw('quick');
  }

  step() {
    const E = this.eng(), m = this.m;
    if (!m || E.top(m).id === 'boot') return;
    const S = m.sessions, pick = (a) => a[Math.floor(Math.random() * a.length)], t = Date.now();
    const TOOLS = ['Edit src/app.ts', 'Read package.json', 'Bash: npm run build', 'Grep "TODO" src/', 'Write tests/api.test.ts', 'Read src/main.cpp', 'Bash: npm run test'];
    const SUMMARIES = ['Endpoint ordini completato, 12 test verdi.', 'Layout mobile del carrello sistemato.', 'Driver del display aggiornato e provato.', 'Documentazione rigenerata, nessun link rotto.', 'Refactoring finito: tre file più corti.'];
    if (!m.sys.pc) return;
    S.forEach((s) => { s._t = (s._t || 0) + 1; });
    m.archived.forEach((s) => { s._t = (s._t || 0) + 1; });
    const dh5 = 0.3 + Math.random() * 0.6, dd7 = 0.05 + Math.random() * 0.1;  // stessi numeri casuali anche senza limiti
    if (m.limits.h5 != null) m.limits.h5 = Math.min(100, m.limits.h5 + dh5);
    if (m.limits.d7 != null) m.limits.d7 = Math.min(100, m.limits.d7 + dd7);
    if (m.acc) {
      const before = m.air.co2;
      m.air.co2 = Math.max(450, Math.min(1500, m.air.co2 + Math.round((Math.random() - 0.35) * 60)));
      if (before <= 1000 && m.air.co2 > 1000) E.toast(m, 'CO2 alta: apri la finestra');
    }
    const comp = S.find((s) => s.state === 'compact' && s._t >= 2);
    if (comp) {
      comp.state = 'working'; comp.ctx = 22; comp.detail = pick(TOOLS); comp._t = 0;
      E.logEv(m, comp, 'memoria riordinata, contesto al 22%');
      this.changed(); return;
    }
    const tp = E.top(m), busy = tp.id === 'perm' ? tp.i : -1;
    const waiting = S.filter((s, i) => E.isAttn(s) && s._t >= 4 && i !== busy);
    if (waiting.length && Math.random() < 0.4) {
      const s = pick(waiting);
      if (s.state === 'permission') m.stats.permTerminal++;
      E.addWait(m, s);
      s.state = 'working'; s.detail = pick(TOOLS); s._t = 0;
      E.logEv(m, s, 'risposto dal terminale');
      this.changed(); return;
    }
    const fin = S.filter((s) => s.state === 'done' && s._t >= 5).concat(m.archived.filter((s) => s._t >= 5));
    if (fin.length && Math.random() < 0.4) {
      const s = pick(fin), n = 3 + Math.floor(Math.random() * 4), names = ['Analizza il codice', 'Scrivi il test', 'Implementa', 'Rifattorizza', 'Aggiorna la doc', 'Verifica finale'];
      this.revive(s);
      s.tasks = Array.from({ length: n }, (_, i) => [names[i % names.length], i === 0 ? 1 : 0]);
      E.count(s);
      s.state = 'working'; s.detail = pick(TOOLS); s.since = t; s._t = 0; s.summary = ''; s.test = null;
      E.logEv(m, s, 'nuovo prompt, ' + n + ' task');
      this.changed(); return;
    }
    const working = S.filter((s) => s.state === 'working');
    if (!working.length) return;
    const s = pick(working), r = Math.random();
    if (r < 0.1) { this.raise(s, 'permission'); return; }
    if (r < 0.14) { this.raise(s, 'input'); return; }
    if (r < 0.16) { this.raise(s, 'error'); return; }
    s.ctx = Math.min(99, s.ctx + 2 + Math.floor(Math.random() * 5));
    if (s.ctx >= 92) {
      s.state = 'compact'; s.detail = 'Riordina la memoria'; s._t = 0;
      E.logEv(m, s, 'contesto al ' + s.ctx + '%, riordina la memoria');
      this.changed(); return;
    }
    if (Math.random() < 0.2) {
      if (s.subs && Math.random() < 0.5) { s.subs--; E.logEv(m, s, 'subagente finito'); }
      else if (s.subs < 3) { s.subs++; E.logEv(m, s, 'subagente avviato'); }
    }
    m.stats.linesAdd += Math.floor(Math.random() * 40);
    m.stats.linesDel += Math.floor(Math.random() * 15);
    m.stats.cost += Math.random() * 0.25;
    const i = s.tasks.findIndex((x) => x[1] === 1);
    if (i >= 0) { s.tasks[i][1] = 2; if (s.tasks[i + 1]) s.tasks[i + 1][1] = 1; m.stats.tasks++; }
    else { const j = s.tasks.findIndex((x) => x[1] === 0); if (j >= 0) s.tasks[j][1] = 1; }
    E.count(s);
    s.detail = pick(TOOLS);
    if (/^Bash: npm (run )?test/.test(s.detail)) { s.test = Math.random() < 0.7 ? 'ok' : 'ko'; E.logEv(m, s, s.test === 'ok' ? 'test superati' : 'test falliti'); }
    if (s.total && s.done >= s.total) { s.state = 'done'; s.detail = s.total + ' task completati'; s.summary = pick(SUMMARIES); s.subs = 0; s._t = 0; E.logEv(m, s, 'turno completato'); }
    else E.logEv(m, s, 'task ' + s.done + '/' + s.total + ' completato');
    this.changed();
  }
  raise(s, kind) {
    const E = this.eng(), m = this.m;
    const CMDS = ['Bash: npm run test', 'Bash: git push origin main', 'Bash: docker compose up -d', 'Bash: pio run -t upload', 'Bash: rm -rf dist'];
    s.state = kind; s._t = 0; s.attnAt = Date.now();
    // Gioco (0.9.0): una richiesta di permesso interrompe la partita e torna alla Home, che la mostra; la risposta segue
    // le regole di sempre (Dettaglio, 2×, NEGA preselezionato). Il record è già salvato a ogni serie migliore.
    if (kind === 'permission' && E.top(m).id === 'game') { m.ui.stack = [{ id: 'home' }]; E.toast(m, 'Partita interrotta: un permesso'); this.redraw('quick'); }
    if (kind === 'permission') { s.detail = CMDS[Math.floor(Math.random() * CMDS.length)]; E.logEv(m, s, 'chiede permesso: ' + s.detail.replace('Bash: ', '')); }
    else if (kind === 'input') { s.detail = 'Domanda: quale database uso?'; E.logEv(m, s, 'ti ha fatto una domanda'); }
    else { s.detail = 'StopFailure: rate_limit'; E.logEv(m, s, 'errore rate_limit'); }
    if (this.dev.cls !== 'S' && Date.now() - this.lastInputAt > 8000 && E.top(m).id === 'home') m.ui.sel = m.sessions.indexOf(s);
    if ((this.dev.buzzer || m.acc) && m.set.sound && !E.quiet(m) && kind === 'permission') this.beepUntil = Date.now() + 900;
    this.changed();
  }
  changed() {
    if (!this.eng().quiet(this.m)) this.m.burstUntil = Date.now() + 4000;
    this.redraw('partial');
  }
  simEvent(kind) {
    const E = this.eng(), m = this.m;
    const s = m.sessions.find((x) => x.state === 'working') || m.sessions.find((x) => !E.isAttn(x)) || m.sessions[0];
    if (!s) { this.lastInput = 'Simula'; this.lastAction = 'Nessuna sessione: apri Claude Code sul PC'; this.forceUpdate(); return; }
    this.raise(s, kind);
    this.lastInput = 'Simula';
    this.lastAction = ({ permission: 'Richiesta di permesso', input: 'Domanda', error: 'Errore API' })[kind] + ' da ' + s.name;
    this.forceUpdate();
  }

  keyDown(id) {
    this.ensure();
    this.downAt[id] = Date.now();
    this.lastPtr[id] = Date.now();
    this.pressed[id] = true;
    if (this.dev.input === 'single') {
      this.lastInput = 'Tasto premuto…';
      const t = this.downAt[id], E = this.eng();
      this.m.ui.hold = { since: t, done: false, ok: false, next: t + E.HOLD_STEP };
      clearTimeout(this.hTimer);
      this.hTimer = setTimeout(() => this.holdFire(), E.HOLD_MS);  // a 3 s, anche con il tasto ancora giù
    }
    this.forceUpdate();
  }
  // Tenuto 3 s raggiunto con il tasto ancora premuto: l'azione parte subito, il rilascio poi non conta.
  holdFire() {
    const h = this.m && this.m.ui.hold;
    if (!h || h.done || !this.pressed.single) return;
    h.done = true;
    h.ok = !!this.eng().holdLabel(this.m);
    clearTimeout(this.cTimer); this.count = 0;
    this.gesture('vlong');
    if (h.ok) this.redraw('partial');  // «FATTO · RILASCIA» (si unisce al ridisegno dell'azione)
  }
  keyUp(id) {
    if (!this.pressed[id]) return;
    const dur = Date.now() - (this.downAt[id] || Date.now());
    this.pressed[id] = false;
    this.lastPtr[id] = Date.now();
    if (this.dev.input === 'single') {
      const h = this.m.ui.hold, E = this.eng();
      this.m.ui.hold = null;
      clearTimeout(this.hTimer);
      if (h && h.done) {  // l'azione è già partita a 3 s: il rilascio non è un gesto
        this.lastInput = 'Rilasciato';
        if (h.ok) this.redraw('partial');  // via «FATTO · RILASCIA»
        this.forceUpdate();
        return;
      }
      if (h && dur >= 700 && E.holdLabel(this.m)) this.redraw('partial');  // l'indicatore era a schermo: va tolto
      if (dur >= E.HOLD_MS) { clearTimeout(this.cTimer); this.count = 0; this.gesture('vlong'); }
      else if (dur >= 700) { clearTimeout(this.cTimer); this.count = 0; this.gesture('long'); }
      else {
        this.count++;
        clearTimeout(this.cTimer);
        this.lastInput = this.count === 1 ? '1 pressione…' : this.count + ' pressioni…';
        this.cTimer = setTimeout(() => { const k = Math.min(this.count, 3); this.count = 0; this.gesture(k + 'x'); }, 380);
      }
    } else {
      this.button(id, dur >= 3000 ? 'vlong' : dur >= 700 ? 'long' : 'short');
    }
    this.forceUpdate();
  }
  gesture(g) {
    const names = { '1x': '1 pressione', '2x': '2 pressioni', '3x': '3 pressioni', long: 'Pressione lunga', vlong: 'Tenuto 3 secondi' };
    this.lastInput = names[g];
    let r;
    if (g === '1x') r = this.act('next');
    else if (g === '2x') r = this.act('ok');
    else if (g === '3x') r = this.act('menu');
    else if (g === 'long') r = this.act('back', true);
    else r = this.act('sleep');
    this.lastAction = r || '—';
    this.forceUpdate();
  }
  button(id, kind) {
    const E = this.eng(), home = E.top(this.m).id === 'home';
    const L = { up: 'Rotella ▲', down: 'Rotella ▼', ok: 'Rotella premuta', menu: 'MENU', exit: 'EXIT', left: '◀ sinistro', mid: '● centrale', right: '▶ destro' }[id] || id;
    this.lastInput = L + (kind === 'short' ? '' : kind === 'long' ? ' · tenuto' : ' · tenuto 3 s');
    let r;
    if (this.dev.input === 'wheel') {
      if (id === 'up') r = this.act('prev');
      else if (id === 'down') r = this.act('next');
      else if (id === 'ok') r = kind === 'short' ? this.act('ok') : (home ? this.act('mute') : this.act('ok'));
      else if (id === 'menu') r = kind === 'short' ? this.act('menu') : this.act('sleep');
      else if (id === 'exit') r = kind === 'short' ? this.act('back') : this.act('home');
    } else {
      if (id === 'left') r = kind === 'short' ? this.act('prev') : this.act('back');
      else if (id === 'mid') r = kind === 'short' ? this.act('ok') : this.act('menu');
      else if (id === 'right') r = kind === 'short' ? this.act('next') : (home ? this.act('mute') : this.act('home'));
    }
    this.lastAction = r || '—';
  }
  toggleMute() {
    const E = this.eng(), m = this.m;
    if (E.muted(m)) { m.set.mute = 0; m.set.muteKind = ''; E.toast(m, 'Avvisi attivi'); this.redraw('partial'); return 'Avvisi riattivati'; }
    m.set.mute = Date.now() + 3600000; m.set.muteKind = 'hour';
    E.toast(m, 'Silenziato per 1 ora'); this.redraw('partial');
    return 'Silenzia per 1 ora';
  }
  items(tp) {
    const E = this.eng();
    return tp.id === 'wifiScan' ? E.netItems(this.m) : E.menuList(tp.menu, this.dev, this.m, tp.ssid).items;
  }
  act(name, homeMute) {
    const E = this.eng(), m = this.m, dev = this.dev, tp = E.top(m), S = dev.cls === 'S', n = m.sessions.length, t = Date.now();
    this.lastInputAt = t;
    if (tp.id === 'boot') return 'Avvio in corso: tasto ignorato';
    if (tp.id === 'sleep') { m.ui.stack.pop(); this.redraw('quick'); return 'Riattivato'; }
    // M6: batteria scarica (sul firmware il tasto sveglia la scheda, che ricontrolla la batteria).
    if (tp.id === 'battlow') {
      if (!m.sys.usb) return 'Batteria scarica: collega la USB-C';
      m.ui.stack.pop(); this.redraw('quick'); return 'Riattivato';
    }
    // Aggiornamento: durante la ricezione nessun gesto conta; errore e rifiuto si chiudono con qualunque gesto.
    if (tp.id === 'ota') {
      if (m.ota && (m.ota.phase === 'error' || m.ota.phase === 'rejected')) { m.ui.stack.pop(); m.ota = null; this.redraw('quick'); return 'Chiuso'; }
      return 'Aggiornamento in corso: non spegnere';
    }
    // M6: calibrazione in corso: solo 3× la interrompe.
    if (tp.id === 'calib' && E.calRunning(m)) {
      if (name === 'menu') { this.calibAbort('Calibrazione annullata'); return 'Calibrazione annullata'; }
      return 'Calibrazione in corso: 3X per annullare';
    }
    if (!E.muted(m)) m.burstUntil = t + 3000;
    if (name === 'sleep' && tp.id === 'confirm') {
      // Proteggi le chiavi, Ripristino di fabbrica: qui il tenuto 3 s conferma invece di mandare a riposo.
      if (tp.until > t) return this.confirmAction(tp);
      E.toast(m, 'Prima 2X, poi tieni 3 s'); this.redraw('partial');
      return 'Prima 2 pressioni, poi tieni premuto 3 secondi';
    }
    if (name === 'sleep') { m.ui.stack.push({ id: 'sleep' }); this.redraw('quick'); return 'Riposo: orologio e Goccia che dorme'; }
    if (name === 'mute') return this.toggleMute();
    if (name === 'home') { m.ui.stack = [{ id: 'home' }]; this.redraw('quick'); return 'Torna alla Home'; }
    if (name === 'menu') {
      if (tp.id === 'perm') return 'Prima rispondi o annulla';
      if (tp.id === 'pair') return 'Prima conferma o annulla';
      if (tp.id === 'home' || tp.id === 'detail') { m.ui.stack.push({ id: 'list', menu: 'main', sel: 0 }); this.redraw('quick'); return 'Apri il Menu'; }
      m.ui.stack = [{ id: 'home' }]; this.redraw('quick');
      return 'Torna alla Home';
    }
    if (name === 'back') {
      // Conferma armata (dopo il 2×): rilasciato fra 0,7 e 3 s non torna indietro, resta armata altri 10 s.
      if (homeMute && tp.id === 'confirm' && tp.until > t) { tp.until = t + 10000; E.toast(m, 'Troppo presto: tieni fino in fondo'); this.redraw('partial'); return 'Rilasciato prima dei 3 s: la conferma resta armata'; }
      // Nessuna scadenza: si chiude senza rispondere, la richiesta resta aperta qui (Dettaglio, 2×) e nel terminale.
      if (tp.id === 'perm') { m.ui.stack.pop(); E.toast(m, 'Annullato · resta in attesa'); this.redraw('quick'); return 'Annulla: la richiesta resta aperta, qui e nel terminale'; }
      if (tp.id === 'pair') { m.ui.stack.pop(); E.toast(m, 'Abbinamento annullato'); E.logEv(m, null, 'abbinamento annullato sul display'); this.redraw('quick'); return 'Abbinamento annullato: nessuna chiave salvata'; }
      if (m.ui.stack.length > 1) { m.ui.stack.pop(); this.redraw('quick'); return 'Indietro'; }
      if (homeMute) return this.toggleMute();
      return 'Sei già nella Home';
    }
    if (tp.id === 'game' && (name === 'next' || name === 'ok')) return this.gameAct(name === 'next');
    if (name === 'next' || name === 'prev') {
      const d = name === 'next' ? 1 : -1;
      if (!m.sys.pc && (tp.id === 'home' || tp.id === 'detail')) return 'PC non raggiungibile: apri il Menu';
      if (n === 0 && (tp.id === 'home' || tp.id === 'detail')) return 'Nessuna sessione';
      if (tp.id === 'home') {
        const base = S ? (m.set.homeStyle === 'una' ? E.unaIdx(m) : E.sIdx(m)) : m.ui.sel;
        m.ui.sel = (base + d + n) % n; m.ui.page = m.ui.sel;
        if (S) m.ui.selUntil = t + 15000;
        this.redraw('partial');
        return 'Seleziona ' + m.sessions[m.ui.sel].name;
      }
      if (tp.id === 'detail') { m.ui.sel = (m.ui.sel + d + n) % n; this.redraw('partial'); return 'Sessione ' + m.sessions[m.ui.sel].name; }
      if (tp.id === 'perm') { tp.choice = 1 - tp.choice; this.redraw('partial'); return 'Scelta: ' + (tp.choice === 0 ? 'CONSENTI' : 'NEGA'); }
      if (tp.id === 'list' || tp.id === 'wifiScan') {
        if (tp.scanUntil > t) return 'Ricerca in corso';
        const items = this.items(tp);
        if (!items.length) return 'Elenco vuoto';
        tp.sel = ((tp.sel || 0) + d + items.length) % items.length;
        this.redraw('partial');
        return 'Voce: ' + items[tp.sel].label;
      }
      if (tp.id === 'storico' || tp.id === 'servizi') {
        const len = tp.id === 'storico' ? m.log.length : 2 + m.services.length + m.prs.length, stepN = tp.id === 'storico' ? 3 : 2;
        const max = Math.max(0, len - 4);
        tp.off = (tp.off || 0) + d * stepN;
        if (tp.off > max) tp.off = 0;
        if (tp.off < 0) tp.off = max;
        this.redraw('partial');
        return 'Scorri lo storico';
      }
      return 'Niente da scorrere qui';
    }
    if (name === 'ok') {
      if (!m.sys.pc && (tp.id === 'home' || tp.id === 'detail')) return 'PC non raggiungibile: apri il Menu';
      if (n === 0 && (tp.id === 'home' || tp.id === 'detail')) return 'Nessuna sessione';
      if (tp.id === 'home') {
        if (S) m.ui.sel = m.set.homeStyle === 'una' ? E.unaIdx(m) : E.sIdx(m);
        m.ui.selUntil = 0;
        m.ui.stack.push({ id: 'detail' }); this.redraw('quick');
        return 'Apri ' + m.sessions[m.ui.sel].name;
      }
      if (tp.id === 'detail') {
        const s = m.sessions[m.ui.sel];
        if (s.state === 'permission') {
          if (!m.set.permOn) { E.toast(m, 'Risposte dal display spente'); this.redraw('partial'); return 'Risposte dal display disattivate (Menu › Avvisi)'; }
          if (!E.permFromDisplay(m)) { E.toast(m, 'Rispondi nel terminale'); this.redraw('partial'); return 'Permessi dal display dopo l\'abbinamento: rispondi nel terminale'; }
          // §7: NEGA preselezionato, due gesti per consentire. Nessuna scadenza (M5).
          const e = { id: 'perm', i: m.ui.sel, choice: 1 };
          m.ui.stack.push(e);
          this.permText = E.permWaitText(E.permSince(m, e)); this.redraw('quick');
          return 'Rispondi al permesso di ' + s.name + ' (NEGA preselezionato)';
        }
        if (s.state === 'input') { E.toast(m, 'Rispondi nel terminale'); this.redraw('partial'); return 'Le domande si rispondono nel terminale'; }
        if (E.canArchive(s)) return this.archive(m.ui.sel);
        return 'Niente da confermare per questa sessione';
      }
      if (tp.id === 'perm') return this.confirmPerm(tp);
      if (tp.id === 'pair') {
        if (tp.sel === 1) return this.pairConfirm();
        E.toast(m, 'Prima scrivi il codice sul PC'); this.redraw('partial');
        return 'Scrivi il codice sul PC: poi qui si conferma con 2 pressioni';
      }
      if (tp.id === 'confirm') { tp.until = t + 10000; this.redraw('partial'); return 'Ora tieni premuto 3 secondi per confermare'; }
      if (tp.id === 'list') return this.activate(tp);
      if (tp.id === 'wifiScan') {
        if (tp.scanUntil > t) return 'Ricerca in corso';
        const it = this.items(tp)[tp.sel || 0];
        if (!it) return '';
        if (it.lock) { m.ui.stack.push({ id: 'qr', ssid: it.net.ssid }); this.redraw('quick'); return 'Configura ' + it.net.ssid + ' dal telefono'; }
        m.ui.stack.push({ id: 'connecting', ssid: it.net.ssid, until: t + 2600 }); this.redraw('quick');
        return 'Connessione a ' + it.net.ssid;
      }
      if (tp.id === 'qr') {
        m.ui.stack.push({ id: 'connecting', ssid: tp.ssid || m.sys.ssid || 'CasaWiFi', until: t + 2600 }); this.redraw('quick');
        return 'Simulato: password inserita dal telefono';
      }
      if (tp.id === 'focus') {
        if (E.focusOn(m)) { m.focus.until = 0; E.toast(m, 'Concentrazione interrotta'); E.logEv(m, null, 'concentrazione interrotta'); this.redraw('partial'); return 'Concentrazione interrotta'; }
        m.focus.until = t + m.focus.len * 60000;
        E.toast(m, 'Concentrazione fino alle ' + new Date(m.focus.until).toTimeString().slice(0, 5));
        E.logEv(m, null, 'concentrazione avviata');
        this.redraw('partial');
        return 'Concentrazione per ' + m.focus.len + ' minuti: avvisi in silenzio';
      }
      if (tp.id === 'batteria') { m.set.save = !m.set.save; this.redraw('partial'); return 'Risparmio energetico: ' + (m.set.save ? 'sì' : 'no'); }
      if (tp.id === 'pet') {
        const wid = m.pet.wear ? m.pet.wear.id : '', k = m.pet.unlocked.findIndex((u) => u.id === wid);
        m.ui.stack.push({ id: 'list', menu: 'petacc', sel: k + 1 }); this.redraw('quick');
        return 'Apri Accessorio';
      }
      return 'Niente da confermare qui';
    }
    return '';
  }
  // «Accessorio» › 2×: indossa (wid = id tra gli sbloccati, '' = nessuno). Sulla scheda parte accessory-wear {id|null}
  // verso il PC, che risponde con un `game` aggiornato; intanto la scheda lo indossa subito. Torna a «La mascotte».
  petWear(wid) {
    const E = this.eng(), m = this.m, p = m.pet, u = wid ? p.unlocked.find((x) => x.id === wid) : null;
    if (wid && !u) return '';
    const a = wid ? E.ACCESSORIES[wid] : null;
    p.wear = wid ? { id: wid, crc: a ? a.crc : null } : null;
    if (E.top(m).id === 'list' && E.top(m).menu === 'petacc') m.ui.stack.pop();
    E.toast(m, u ? 'Indossa: ' + u.name : 'Nessun accessorio');
    this.redraw('quick');
    return u ? 'Accessorio: ' + u.name : 'Accessorio: nessuno';
  }
  // Giornata di lavoro (0.9.0): Menu › Strumenti › Giornata di lavoro › Inizia ora, e il comando `workday` del PC
  // (sul firmware, con l'ack: «orario di fine già passato»). La fine è fissa all'orario di fine di oggi (regola A).
  // Restituisce la risposta per «Ultimo comando»; this.workErr = errore per l'ack ('' = ok).
  workStart() {
    const E = this.eng(), m = this.m, w = m.work, t = Date.now();
    this.workErr = '';
    if (w.on) { E.toast(m, 'Giornata già attiva'); return 'Giornata di lavoro già attiva fino alle ' + E.hm(new Date(w.endAt)); }
    const end = E.dayAt(t, w.end);
    if (t >= end) { this.workErr = 'orario di fine già passato'; E.toast(m, 'Orario di fine già passato'); return 'Giornata di lavoro: le ' + E.minHm(w.end) + ' sono già passate'; }
    w.on = true; w.endAt = end; w.from = t; w.doneUntil = 0; w.party = 0;
    E.toast(m, 'Giornata fino alle ' + E.minHm(w.end));
    E.logEv(m, null, 'giornata di lavoro avviata');
    return 'Giornata di lavoro fino alle ' + E.minHm(w.end) + ': mancano ' + E.workLeftText(m, false);
  }
  workStop() {
    const E = this.eng(), m = this.m, w = m.work;
    this.workErr = '';
    if (!w.on) { E.toast(m, 'Giornata non attiva'); return 'Giornata di lavoro già spenta'; }
    w.on = false; w.doneUntil = 0; w.party = 0;
    E.toast(m, 'Giornata fermata');
    E.logEv(m, null, 'giornata di lavoro fermata');
    return 'Giornata di lavoro fermata';
  }
  // Ogni tick: a mezzanotte si azzera; alla fine «GIORNATA FINITA» (toast, Riposo e Home per un'ora), festa breve
  // (rispetta silenzio, notturno e risparmio come le altre animazioni), poi si spegne da sola.
  workTick(t) {
    const E = this.eng(), m = this.m, w = m.work;
    if (w.on && !E.sameDay(t, w.endAt)) {
      w.on = false; w.doneUntil = 0; w.party = 0;
      E.logEv(m, null, 'giornata di lavoro azzerata a mezzanotte');
      this.redraw('partial');
    } else if (w.on && t >= w.endAt) {
      w.on = false;
      w.doneUntil = Math.min(w.endAt + E.WORK_DONE_MS, E.nextMidnight(w.endAt));
      if (t < w.doneUntil) {
        E.toast(m, 'GIORNATA FINITA', 5000);
        w.party = t + E.WORK_PARTY_MS;
        if (!E.quiet(m)) m.burstUntil = Math.max(m.burstUntil, w.party);
      }
      E.logEv(m, null, 'giornata di lavoro finita');
      this.lastAction = 'Fine della giornata di lavoro';
      this.redraw('partial');
    }
    if (w.doneUntil && t >= w.doneUntil) { w.doneUntil = 0; this.redraw('partial'); }
    if (w.party && t >= w.party) w.party = 0;
  }
  // Gioco «Alto o basso»: 1× più alta, 2× più bassa; a partita finita 1× o 2× ne comincia un'altra.
  gameAct(up) {
    const E = this.eng(), m = this.m, g = m.game;
    if (g.over) { E.gameNew(m); this.redraw('partial'); return 'Nuova partita: ' + E.RANKS[g.card]; }
    const was = E.RANKS[g.card], r = E.gameGuess(m, up), now_ = E.RANKS[g.card];
    this.redraw('partial');
    const what = (up ? 'Più alta' : 'Più bassa') + ': ' + was + ' → ' + now_;
    if (r === 'win') return what + ', giusto · serie ' + g.streak;
    if (r === 'tie') return what + ', pari · serie ' + g.streak;
    if (g.streak > g.startBest) E.logEv(m, null, 'gioco: nuovo record ' + g.best);
    return what + ', sbagliato · serie ' + g.streak + ', record ' + g.best;
  }
  // Archivia la sessione i: sparisce da Home, contatori, liste e Riposo finché step() (o Simula «Nuovo prompt»)
  // non la rimette al lavoro. Sul firmware con la rete parte anche `dismiss` verso il servizio.
  archive(i) {
    const E = this.eng(), m = this.m, s = m.sessions[i];
    m.sessions.splice(i, 1);
    s._t = 0;
    m.archived.push(s);
    E.logEv(m, s, 'archiviata');
    m.ui.stack = [{ id: 'home' }];
    m.ui.sel = Math.min(i, Math.max(0, m.sessions.length - 1));
    m.ui.selUntil = 0;
    m.ui.page = m.sessions.length ? m.ui.page % m.sessions.length : 0;
    E.toast(m, 'Archiviata');
    this.redraw('quick');
    return 'Archiviata: ' + s.name;
  }
  // Nuovo prompt su una sessione ferma o archiviata (che torna in fondo all'elenco).
  revive(s) {
    const i = this.m.archived.indexOf(s);
    if (i >= 0) { this.m.archived.splice(i, 1); this.m.sessions.push(s); }
  }
  confirmPerm(tp) {
    const E = this.eng(), m = this.m, s = m.sessions[tp.i], ok = tp.choice === 0;
    s.state = 'working'; s._t = 0;
    s.detail = ok ? s.detail.replace(/^Bash: /, 'Bash in corso: ') : 'Permesso negato, cerca un’alternativa';
    m.stats.permDisplay++;
    E.addWait(m, s);
    E.logEv(m, s, ok ? 'permesso concesso dal display' : 'permesso negato dal display');
    m.ui.stack.pop();
    E.toast(m, ok ? 'Consentito · inviato a Claude Code' : 'Negato · inviato a Claude Code');
    this.redraw('quick');
    return ok ? 'Consentito: Claude Code prosegue' : 'Negato: Claude cerca un’alternativa';
  }
  // M5: 2× dopo che il PC ha confermato il codice (pair-confirm). Sul firmware parte pair-done e K si salva.
  pairConfirm() {
    const E = this.eng(), m = this.m;
    m.ui.stack.pop();
    m.sys.pair = 'paired'; m.sys.pairHost = 'calamaio.local';
    E.logEv(m, null, 'abbinato a calamaio.local');
    E.toast(m, 'Abbinato');
    this.redraw('quick');
    return 'Abbinato: il display risponde ai permessi di questo PC';
  }
  // Proteggi le chiavi o Ripristino di fabbrica, dopo 2× e tieni 3 s. Sul firmware: eFuse e NVS veri.
  confirmAction(tp) {
    const E = this.eng(), m = this.m;
    m.ui.stack.pop();
    if (tp.kind === 'protect') {
      m.sys.keys = true;
      E.logEv(m, null, 'chiavi protette nel chip');
      E.toast(m, 'Chiavi protette');
      this.redraw('quick');
      return 'Chiavi protette: segreti cifrati con la chiave nel chip';
    }
    const y = m.sys;
    m.set = E.defaultSet();
    y.wifi = false; y.ssid = ''; y.pc = false; y.lastPc = Date.now(); y.pair = 'none'; y.pairHost = '';
    m.saved = [];
    m.ui.forgetUntil = 0; m.ui.forgetNetUntil = 0; m.ui.unpairUntil = 0;
    m.ui.stack = [{ id: 'boot', until: Date.now() + 2600 }]; y.bootAt = Date.now();
    this.redraw('quick');
    return 'Ripristino di fabbrica: Wi-Fi, abbinamento e impostazioni cancellati';
  }
  activate(tp) {
    const E = this.eng(), m = this.m, it = this.items(tp)[tp.sel || 0];
    if (!it) return '';
    if (it.act !== 'forget') m.ui.forgetUntil = 0;  // un'altra voce annulla la conferma in corso
    if (it.act !== 'forgetnet') m.ui.forgetNetUntil = 0;
    if (it.act !== 'unpair') m.ui.unpairUntil = 0;
    if (it.act !== 'newbatt') m.ui.newBattUntil = 0;
    if (it.go) { const e = { id: 'list', menu: it.go, sel: 0 }; if (it.ssid) e.ssid = it.ssid; m.ui.stack.push(e); this.redraw('quick'); return 'Apri ' + it.label; }
    if (it.open) {
      const scr = { id: it.open, sel: 0, off: 0 };
      if (it.open === 'wifiScan') scr.scanUntil = Date.now() + 1500;
      if (it.open === 'game') E.gameNew(m);
      m.ui.stack.push(scr); this.redraw('quick');
      return 'Apri ' + it.label;
    }
    if (it.act === 'wear') return this.petWear(it.wear);
    if (it.act) return this.doAct(it.act);
    return it.label + ': solo lettura';
  }
  doAct(id) {
    const E = this.eng(), m = this.m, st = m.set;
    const styles = ['goccia', 'righe', 'contatori', 'una'], anims = ['auto', 'raffiche', 'continue', 'spente'];
    let msg = '';
    if (id === 'style') { st.homeStyle = styles[(styles.indexOf(st.homeStyle) + 1) % styles.length]; msg = 'Stile home: ' + E.HOME_LABEL[st.homeStyle]; }
    else if (id === 'anim') { st.anim = anims[(anims.indexOf(st.anim) + 1) % anims.length]; msg = 'Animazioni: ' + E.ANIM_LABEL[st.anim]; }
    else if (id === 'speed') { st.speed = st.speed === 'massima' ? 'profilo' : 'massima'; msg = 'Velocità animazioni: ' + E.SPEED_LABEL[st.speed]; }
    else if (id === 'save') { st.save = !st.save; msg = 'Risparmio energetico: ' + (st.save ? 'sì' : 'no'); }
    else if (id === 'pairinfo') { E.toast(m, 'Avvia dal PC: calamaio pair'); msg = 'L\'abbinamento parte dal PC: calamaio pair o Abbina nella webapp'; }
    else if (id === 'unpair') {
      if (m.sys.pair === 'none') { E.toast(m, 'Nessun abbinamento'); msg = 'Dimentica abbinamento: il display non è abbinato'; }
      // Come «Dimentica rete»: seconda doppia pressione entro 6 s.
      else if (!(m.ui.unpairUntil > Date.now())) { m.ui.unpairUntil = Date.now() + 6000; E.toast(m, '2 pressioni ancora per confermare'); msg = 'Dimentica abbinamento: conferma con altre 2 pressioni'; }
      else { m.ui.unpairUntil = 0; m.sys.pair = 'none'; m.sys.pairHost = ''; E.logEv(m, null, 'abbinamento dimenticato'); E.toast(m, 'Abbinamento dimenticato'); msg = 'Abbinamento dimenticato: permessi solo nel terminale'; }
    }
    else if (id === 'protect' || id === 'factory') {
      if (id === 'protect' && m.sys.keys) { E.toast(m, 'Chiavi già protette'); msg = 'Le chiavi sono già protette dalla chiave nel chip'; }
      else {
        m.ui.stack.push({ id: 'confirm', kind: id, until: 0 }); this.redraw('quick');
        return id === 'protect' ? 'Proteggi le chiavi: leggi l\'avviso, poi 2X e tieni 3 s' : 'Ripristino di fabbrica: leggi l\'avviso, poi 2X e tieni 3 s';
      }
    }
    else if (id === 'invert') { st.invert = !st.invert; this.redraw('quick'); return 'Inverti colori: ' + (st.invert ? 'sì' : 'no'); }
    else if (id === 'rot') { st.rot = !st.rot; this.redraw('quick'); return 'Ruota 180°: ' + (st.rot ? 'sì' : 'no'); }
    else if (id === 'full') { setTimeout(() => { if (this.scr) this.scr.full(m, this.fr(), this.ctl); }, 140); return 'Refresh completo'; }
    else if (id === 'mute') {
      if (!E.muted(m)) { st.mute = Date.now() + 3600000; st.muteKind = 'hour'; msg = 'Silenzia: 1 ora'; }
      else if (st.muteKind === 'hour') { const d = new Date(); if (d.getHours() >= 8) d.setDate(d.getDate() + 1); d.setHours(8, 0, 0, 0); st.mute = d.getTime(); st.muteKind = 'night'; msg = 'Silenzia: fino alle 8'; }
      else { st.mute = 0; st.muteKind = ''; msg = 'Avvisi attivi'; }
    }
    else if (id === 'night') { st.night = !st.night; msg = 'Notturno: ' + (st.night ? 'sì' : 'no'); }
    else if (id === 'theme') { msg = 'Tema: Calamaio'; }
    else if (id === 'perm') { st.permOn = !st.permOn; msg = 'Permessi dal display: ' + (st.permOn ? 'sì' : 'no'); }
    else if (id === 'sound') { st.sound = !st.sound; msg = 'Cicalino: ' + (st.sound ? 'sì' : 'no'); }
    else if (id === 'led') { st.led = !st.led; msg = 'LED: ' + (st.led ? 'sì' : 'no'); }
    else if (id === 'hook') { st.hookDirect = !st.hookDirect; msg = 'Hook diretti: ' + (st.hookDirect ? 'sì' : 'no'); }
    else if (id === 'game') {
      // Parte `settings` con game; il servizio risponde con un `game` (on). Intanto la scheda lo dà per fatto (pet.on).
      E.petSwitch(m, !st.game);
      m.pet.on = st.game;
      msg = 'Gioco della mascotte: ' + (st.game ? 'sì' : 'no');
    }
    else if (id === 'forget') {
      // Operazione distruttiva: serve una seconda doppia pressione entro 6 s (come il firmware).
      if (!(m.ui.forgetUntil > Date.now())) { m.ui.forgetUntil = Date.now() + 6000; E.toast(m, '2 pressioni ancora per confermare'); msg = 'Dimentica tutte le reti: conferma con altre 2 pressioni'; }
      else { m.ui.forgetUntil = 0; m.saved = []; m.sys.wifi = false; m.sys.pc = false; m.sys.lastPc = Date.now(); m.sys.ssid = ''; E.toast(m, 'Reti dimenticate'); msg = 'Tutte le reti Wi-Fi dimenticate'; }
    }
    else if (id === 'forgetnet') {
      // Una rete di Reti salvate: come «Dimentica tutte le reti», seconda doppia pressione entro 6 s. Sul firmware poi
      // pulizia profonda della NVS e riavvio dopo 3 s (riparte con la rete salvata più forte, o in configurazione).
      const ssid = E.top(m).ssid || '';
      if (!(m.ui.forgetNetUntil > Date.now())) { m.ui.forgetNetUntil = Date.now() + 6000; E.toast(m, '2 pressioni ancora per confermare'); msg = 'Dimentica ' + ssid + ': conferma con altre 2 pressioni'; }
      else {
        m.ui.forgetNetUntil = 0;
        m.saved = m.saved.filter((x) => x.ssid !== ssid);
        if (m.sys.wifi && m.sys.ssid === ssid) { m.sys.wifi = false; m.sys.pc = false; m.sys.lastPc = Date.now(); m.sys.ssid = ''; }
        m.ui.stack.pop();
        E.toast(m, 'Rete dimenticata');
        this.redraw('quick');
        return 'Rete ' + ssid + ' dimenticata';
      }
    }
    else if (id === 'calib') {
      // M6: serve la batteria almeno al 90% (col cavo si controlla quando lo stacchi).
      if (!m.sys.usb && m.sys.batt < E.CAL_MIN_PCT) { E.toast(m, 'Carica almeno al 90%'); msg = 'Calibrazione: serve la batteria almeno al 90%'; }
      else {
        const t = Date.now();
        m.calib = { phase: m.sys.usb ? 'wait' : 'max', ms: 0, next: 0, last: t, paused: false, samples: [], mv: 0, rMax: null };
        m.ui.stack.push({ id: 'calib' });
        E.logEv(m, null, m.sys.usb ? 'calibrazione: stacca il cavo' : 'calibrazione: fase 1');
        if (!m.sys.usb) this.calibSample(E.mvFromPct(m.sys.batt), t);
        this.redraw('quick');
        return m.sys.usb ? 'Calibrazione: stacca il cavo USB-C per iniziare' : 'Calibrazione: fase 1, massimo consumo';
      }
    }
    else if (id === 'newbatt') {
      // Come «Dimentica rete»: seconda doppia pressione entro 6 s, poi propone di calibrare.
      if (!(m.ui.newBattUntil > Date.now())) { m.ui.newBattUntil = Date.now() + 6000; E.toast(m, '2 pressioni ancora per confermare'); msg = 'Nuova batteria: conferma con altre 2 pressioni'; }
      else {
        m.ui.newBattUntil = 0; m.bat.cal = null; m.bat.sleep = null;
        E.top(m).sel = 1;
        E.logEv(m, null, 'nuova batteria: calibrazione cancellata');
        E.toast(m, 'Ora calibra la batteria');
        msg = 'Nuova batteria: calibrazione cancellata, ora calibra';
      }
    }
    else if (id === 'workstart') { msg = this.workStart(); this.redraw('partial'); return msg; }
    else if (id === 'workstop') { msg = this.workStop(); this.redraw('partial'); return msg; }
    else if (id === 'reboot') { m.ui.stack = [{ id: 'boot', until: Date.now() + 2600 }]; m.sys.bootAt = Date.now(); this.redraw('quick'); return 'Riavvio'; }
    else if (id === 'sleep') { m.ui.stack.push({ id: 'sleep' }); this.redraw('quick'); return 'Riposo'; }
    else return '';
    this.redraw('partial');
    return msg;
  }
  onTap(e) {
    if (!this.scr) return;
    if (!this.touch) {
      if (this.dev.touch) { this.lastInput = 'Tocco'; this.lastAction = 'Il tocco è spento: attivalo da Simula'; this.forceUpdate(); }
      return;
    }
    const r = e.currentTarget.getBoundingClientRect(), dev = this.dev;
    let x = (e.clientX - r.left) / r.width * dev.W, y = (e.clientY - r.top) / r.height * dev.H;
    if (this.m.set.rot) { x = dev.W - x; y = dev.H - y; }
    const tp = this.eng().top(this.m);
    this.lastInput = 'Tocco';
    if (tp.id === 'sleep' || tp.id === 'boot') { this.lastAction = this.act('ok'); this.forceUpdate(); return; }
    let h = null;
    for (let i = this.scr.hits.length - 1; i >= 0; i--) {
      const q = this.scr.hits[i];
      if (x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h) { h = q; break; }
    }
    this.lastAction = h ? (h.fn() || 'Tocco') : 'Niente da toccare qui';
    this.forceUpdate();
  }
  sims() {
    const C = this.simClicks, s = this.m.sys;
    const tog = (label, click, v) => ({ label: label, click: click, pressed: v, bg: v ? '#2646C4' : 'transparent', fg: v ? '#FFFFFF' : '#2646C4' });
    const btn = (label, click) => ({ label: label, click: click, pressed: undefined, bg: 'transparent', fg: '#2646C4' });
    const list = [
      btn('Nuovo permesso', C.perm),
      btn('Domanda', C.input),
      btn('Errore API', C.error),
      tog('Sessioni automatiche', C.auto, this.auto),
      tog('PC scollegato', C.pc, !s.pc),
      tog('Wi‑Fi assente', C.wifi, !s.wifi),
      tog('USB in carica', C.usb, s.usb),
      btn('Batteria −20%', C.batt),
      btn('Batteria 100 · 50 · 10%', C.battlvl),
      tog('PC spento', C.pcoff, s.pcOff),
      btn('Batteria scarica', C.battcrit),
      tog('Ora non ancora nota', C.noclock, s.wallOk === false),
      tog('Batteria calibrata', C.calsaved, !!this.m.bat.cal),
      btn('Aggiornamento (fasi)', C.ota),
      tog('Giornata di lavoro (dal PC)', C.work, this.m.work.on),
      btn('Giornata: ultimi 25 min', C.worklast),
      btn('Giornata: salta alla fine', C.workend),
      btn('Gioco: seme fisso', C.gameseed),
      tog('Gioco della mascotte (dal PC)', C.game, this.m.set.game),
      btn('Mascotte: sale di livello', C.petlevel),
      btn('Mascotte: traguardo', C.petgoal),
      btn('Mascotte: indossa', C.petwear),
      tog('Mascotte stanca', C.pettired, this.m.pet.tired),
      tog('Mascotte: speciale da chiedere', C.petspecial, this.m.pet.special),
      btn('Limite 5h al 92%', C.limit),
      tog('Accessori I²C', C.acc, this.m.acc),
      btn('Aria viziata', C.air),
      tog('Nessuna sessione', C.empty, !this.m.sessions.length),
      tog('Limiti assenti', C.nolimits, this.m.limits.h5 == null && this.m.limits.d7 == null),
      btn('Connessione fallita', C.joinfail),
      btn('Risposto nel terminale', C.elsewhere),
      btn('Nuovo prompt', C.prompt),
      btn('Abbinamento dal PC', C.pair),
      btn('Codice giusto sul PC', C.pairok),
      btn('Codice sbagliato sul PC', C.pairbad),
      tog('Abbinamento da rifare', C.redo, s.pair === 'redo'),
      btn('Avatar della sessione', C.avatar),
      btn('Avatar predefinito', C.defavatar),
      btn('Refresh completo', C.full)
    ];
    if (this.dev.touch) list.push(tog('Tocco attivo', C.touch, this.touch));
    return list;
  }
  renderVals() {
    this.ensure();
    const E = this.eng(), dev = this.dev, m = this.m;
    const key = (id) => ({
      down: this.kh[id].down, up: this.kh[id].up, leave: this.kh[id].leave, kd: this.kh[id].kd, ku: this.kh[id].ku, click: this.kh[id].click,
      tf: this.pressed[id] ? 'translateY(2px)' : 'none',
      sh: this.pressed[id] ? '0 0 0 #000' : '0 2px 0 #000'
    });
    const cls = { S: 'schermo piccolo · 250×122', M: 'schermo medio · 400×300', W: 'striscia · 792×272', L: 'schermo grande · ' + dev.W + '×' + dev.H }[dev.cls];
    const controls = E.CONTROLS[dev.input].concat(dev.touch ? [['Tocco', 'Tocca una riga per aprirla (solo versione touch)']] : []).map((r) => ({ k: r[0], t: r[1] }));
    return {
      uid: this.uid,
      clsLabel: cls, devName: dev.name, devSize: dev.size, spec: dev.spec, note: dev.note,
      isAcrylic: dev.shell === 'acrylic', isPcb: dev.shell === 'pcb', isCrow: dev.shell === 'crow', isReterm: dev.shell === 'reterm',
      devW: dev.W, devH: dev.H, cssW: Math.round(dev.W * dev.scale), cssH: Math.round(dev.H * dev.scale),
      rendering: dev.scale >= 1 ? 'pixelated' : 'auto', cursor: this.touch ? 'pointer' : 'default',
      setCanvas: this.setCanvas, tap: this.tap, noCtx: this.noCtx,
      keyName: dev.keyName || '',
      k1: key('single'), kUp: key('up'), kDown: key('down'), kOk: key('ok'), kMenu: key('menu'), kExit: key('exit'), kL: key('left'), kM: key('mid'), kR: key('right'),
      controls: controls,
      lastInput: this.lastInput, lastAction: this.lastAction,
      sims: this.sims(),
      profile: E.profileText(dev, m),
      partials: this.scr ? this.scr.partials : 0, fullEvery: dev.fullEvery,
      ledColor: this.ledOn ? '#F5C542' : '#8A9098', ledGlow: this.ledOn ? '0 0 10px #F5C542' : 'none',
      beepText: this.beepUntil > Date.now() ? '♪ bip' : '',
      beepLine: this.beepUntil > Date.now() ? '♪ bip dal ' + (dev.buzzer ? 'cicalino' : 'buzzer esterno') : ''
    };
  }
}
window.CalamaioTest = { template: template, Component: Component, styles: "body{margin:0}\na{color:#2646C4}a:hover{color:#1A3290}" };
})();
