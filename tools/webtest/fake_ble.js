// A fake Web Bluetooth clock for testing the page: speaks the packet protocol of src/ble.h and
// answers by forwarding each request to the mock HTTP server. Small notify packets (60 bytes)
// so long answers are split into many pieces.
(function () {
  const log = [];
  window.__bleLog = log;
  const listeners = [];
  let inbox = [], inId = -1;
  const enc = new TextEncoder(), dec = new TextDecoder();
  function notify(bytes) {
    const dv = new DataView(bytes.buffer.slice(0));
    for (const f of listeners) f({ target: { value: dv } });
    if (rsp.oncharacteristicvaluechanged) rsp.oncharacteristicvaluechanged({ target: { value: dv } });
  }
  async function answer(id, text) {
    const nl = text.indexOf("\n");
    const head = text.slice(0, nl < 0 ? text.length : nl), body = nl < 0 ? "" : text.slice(nl + 1);
    const sp = head.indexOf(" "), method = head.slice(0, sp), path = head.slice(sp + 1);
    log.push(method + " " + path + " (" + body.length + " bytes body)");
    const r = await fetch(path, method === "POST" ? { method, body, headers: { "Content-Type": "application/json" } } : {});
    const data = enc.encode(r.status + "\n" + await r.text());
    const CH = 60;
    for (let p = 0; p < data.length; p += CH) {
      const part = data.subarray(p, p + CH), m = new Uint8Array(part.length + 2);
      m[0] = id; m[1] = (p === 0 ? 1 : 0) | (p + CH >= data.length ? 2 : 0); m.set(part, 2);
      await new Promise(r => setTimeout(r, 2));
      notify(m);
    }
  }
  const req = {
    async writeValueWithResponse(m) {
      if (m[1] & 1) { inbox = []; inId = m[0]; }
      if (m[0] !== inId) return;
      inbox.push(...m.slice(2));
      if (m[1] & 2) setTimeout(() => answer(inId, dec.decode(new Uint8Array(inbox))), 5);
    }
  };
  // like Chrome: the same characteristic object on every connect, with both ways of listening
  const rsp = { addEventListener: (n, f) => listeners.push(f), startNotifications: async () => {}, oncharacteristicvaluechanged: null };
  const device = {
    name: "MATRIX-CLOCK",
    addEventListener() {},
    gatt: { connect: async () => ({ getPrimaryService: async () => ({ getCharacteristic: async u => u.startsWith("c1a70002") ? req : rsp }) }) },
  };
  Object.defineProperty(navigator, "bluetooth", { configurable: true, value: { requestDevice: async () => device, getDevices: async () => [device] } });
})();
