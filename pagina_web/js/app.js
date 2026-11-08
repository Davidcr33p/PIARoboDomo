// ===== Raulcito bracito: panel en línea =====
// La página y el ESP32 se comunican por Firebase Realtime Database:
//   robot/estado              <- el robot lo sube cada 1 s
//   robot/eventos             <- últimos 20 eventos
//   robot/ordenes/comando     -> botón presionado; el robot contesta en robot/respuesta
//   robot/ordenes/config      -> IDs de SinricPro, URL y nombre
//   robot/ordenes/secretos    -> APP_KEY y APP_SECRET (se escriben, no se leen)
//   dispositivo, solicitudes  -> vinculación del robot (solo administradores)
//   equipo                    -> quién puede entrar
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getAuth, onAuthStateChanged, signInWithEmailAndPassword, createUserWithEmailAndPassword,
  sendPasswordResetEmail, signOut
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-auth.js";
import {
  getDatabase, ref, onValue, set, update, remove, serverTimestamp
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig } from "./firebase-config.js";

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
auth.languageCode = "es";
const db = getDatabase(app);

// ---------- Utilidades ----------
const $ = id => document.getElementById(id);
const VISTAS = ["vCargando", "vLogin", "vSinAcceso", "vPanel"];
function ver(id, si) { $(id).classList.toggle("oculto", !si); }
function vista(id) { VISTAS.forEach(v => ver(v, v === id)); }
function el(t, c, x) { const e = document.createElement(t); if (c) e.className = c; if (x != null) e.textContent = x; return e; }
function toast(t) { const e = $("toast"); e.textContent = t; e.classList.add("ver"); clearTimeout(e._t); e._t = setTimeout(() => e.classList.remove("ver"), 2800); }
function tag(e, on, a, b) { e.textContent = on ? a : b; e.className = "tag " + (on ? "si" : "no"); }
function seg(n) { n = Math.max(0, Math.round(n)); return n < 60 ? n + " s" : n < 3600 ? Math.floor(n / 60) + " min" : n < 86400 ? Math.floor(n / 3600) + " h" : Math.floor(n / 86400) + " d"; }
function calidad(r) { return r > -55 ? "excelente" : r > -67 ? "buena" : r > -78 ? "regular" : "débil"; }
function clave(correo) { return correo.trim().toLowerCase().replaceAll(".", ","); }
function pill(texto, tipo) { const p = $("hEstado"); p.textContent = texto; p.className = "pill" + (tipo ? " " + tipo : ""); }
function ahora() { return Date.now() + offset; }   // hora del servidor

const ERRORES = {
  "auth/invalid-credential": "Correo o contraseña incorrectos.",
  "auth/wrong-password": "Correo o contraseña incorrectos.",
  "auth/user-not-found": "Correo o contraseña incorrectos.",
  "auth/invalid-email": "El correo no es válido.",
  "auth/email-already-in-use": "Ese correo ya tiene cuenta: entra o recupera tu contraseña.",
  "auth/weak-password": "La contraseña debe tener al menos 6 caracteres.",
  "auth/too-many-requests": "Demasiados intentos. Espera un momento.",
  "auth/network-request-failed": "Sin conexión a internet.",
  "PERMISSION_DENIED": "No tienes permiso para hacer eso."
};
const mensaje = e => ERRORES[e.code] || ERRORES[e.message] || e.message;

// ---------- Estado de la página ----------
let usuario = null, esAdmin = false, panelListo = false, offset = 0;
let estado = null, dispositivo = null, solicitudes = {}, equipo = {};
let suscripciones = [];
const esperando = new Map();   // n del comando -> resolver la promesa con la respuesta

function escuchar(ruta, cb, alFallar) {
  suscripciones.push(onValue(ref(db, ruta), cb, alFallar || (e => console.warn(ruta, e.message))));
}
function detenerDatos() { suscripciones.forEach(f => f()); suscripciones = []; panelListo = false; }

// ---------- Sesión ----------
let creando = false;
$("bModo").onclick = () => {
  creando = !creando;
  $("tLogin").textContent = creando ? "Crear cuenta" : "Entrar";
  $("bLogin").textContent = creando ? "Crear cuenta" : "Entrar";
  $("bModo").textContent = creando ? "¿Ya tienes cuenta? Entrar" : "¿No tienes cuenta? Crear una";
  $("lClave").autocomplete = creando ? "new-password" : "current-password";
  $("lErr").textContent = "";
};

$("fLogin").onsubmit = async e => {
  e.preventDefault();
  const correo = $("lCorreo").value.trim(), pass = $("lClave").value;
  if (!correo || pass.length < 6) { $("lErr").textContent = "Escribe tu correo y una contraseña de al menos 6 caracteres."; return; }
  $("bLogin").disabled = true; $("lErr").textContent = "";
  try {
    if (creando) await createUserWithEmailAndPassword(auth, correo, pass);
    else await signInWithEmailAndPassword(auth, correo, pass);
  } catch (x) { $("lErr").textContent = mensaje(x); }
  finally { $("bLogin").disabled = false; }
};

$("bOlvide").onclick = async () => {
  const correo = $("lCorreo").value.trim();
  if (!correo) { $("lErr").textContent = "Escribe tu correo arriba y vuelve a tocar «Olvidé mi contraseña»."; return; }
  try { await sendPasswordResetEmail(auth, correo); toast("Te enviamos un correo para cambiar tu contraseña."); }
  catch (x) { $("lErr").textContent = mensaje(x); }
};

$("bSalir").onclick = () => signOut(auth);
$("bReintentar").onclick = () => iniciarDatos();

onAuthStateChanged(auth, u => {
  detenerDatos();
  usuario = u;
  ver("bSalir", !!u);
  if (!u) { vista("vLogin"); pill("Sin sesión"); $("hSub").textContent = "Panel en línea"; return; }
  iniciarDatos();
});

// ---------- Acceso: solo quien está en /equipo ----------
function iniciarDatos() {
  detenerDatos();
  vista("vCargando");
  const k = clave(usuario.email);
  escuchar("equipo", s => {
    equipo = s.val() || {};
    if (!equipo[k]) return sinAcceso();
    esAdmin = equipo[k].admin === true;
    if (!panelListo) arrancarPanel();
    pintarEquipo();
  }, () => sinAcceso());
}

function sinAcceso() {
  detenerDatos();
  $("saCorreo").textContent = usuario.email;
  vista("vSinAcceso");
  pill("Sin acceso");
}

function arrancarPanel() {
  panelListo = true;
  vista("vPanel");
  ver("fEquipo", esAdmin);
  escuchar(".info/serverTimeOffset", s => { offset = s.val() || 0; });
  escuchar("robot/estado", s => { estado = s.val(); pintarEstado(); });
  escuchar("robot/eventos", s => pintarEventos(s.val()));
  escuchar("robot/respuesta", s => {
    const r = s.val();
    if (r && esperando.has(r.n)) { esperando.get(r.n)(r); esperando.delete(r.n); }
  });
  escuchar("robot/ordenes/config", s => llenarConfig(s.val() || {}));
  escuchar("dispositivo", s => { dispositivo = s.val(); pintarVinculacion(); pintarEstado(); });
  if (esAdmin) escuchar("solicitudes", s => { solicitudes = s.val() || {}; pintarVinculacion(); });
  else { solicitudes = {}; pintarVinculacion(); }
}

// ---------- Estado del robot ----------
function enLinea() {
  if (!dispositivo || !estado || !estado.ts) return false;
  // ahora() = Date.now() + offset (offset llega de .info/serverTimeOffset)
  // El ESP32 puede tardar hasta ~76 s entre actualizaciones aceptadas por Firebase.
  // Damos margen hasta 120 s para no marcarlo offline entre latidos.
  const dif = ahora() - estado.ts;
  return dif < 120000 && dif > -60000;
}

function pintarEstado() {
  if (!panelListo) return;
  const s = estado || {}, vivo = enLinea();
  $("hSub").textContent = s.nombre || "Panel en línea";
  if (!dispositivo) pill("Sin robot");
  else if (vivo) pill("En línea", "on");
  else pill("Desconectado", "off");

  const off = $("avisoOff");
  if (dispositivo && !vivo) {
    off.textContent = s.ts
      ? "El robot está desconectado. Última señal hace " + seg((ahora() - s.ts) / 1000) + ". Los botones se activan cuando vuelva."
      : "El robot está vinculado pero todavía no ha enviado datos. Revisa que esté encendido y con Wi-Fi.";
    ver("avisoOff", true);
  } else ver("avisoOff", false);

  $("sDist").textContent = s.dist == null || s.dist < 0 ? "–" : Number(s.dist).toFixed(1);
  $("sPir").textContent = s.pir == null ? "–" : s.pir ? "Sí" : "No";
  $("sLuz").textContent = s.luz ?? "–";
  $("sLuzT").textContent = s.oscuro == null ? "Luz" : "Luz · " + (s.oscuro ? "oscuro" : "claro");
  tag($("rLimp"), !!s.limpiando, "Activa", "Detenida");
  const b = $("bLimp");
  b.textContent = s.limpiando ? "Detener limpieza" : "Iniciar limpieza";
  b.dataset.cmd = s.limpiando ? "LIMPIEZA_OFF" : "LIMPIEZA_ON";
  $("rBrazo").textContent = s.brazo == null ? "–" : s.brazo ? "Recogiendo…" : "Libre";
  $("rArd").textContent = s.arduino ? s.arduino + (s.arduino_seg >= 0 ? " · hace " + seg(s.arduino_seg) : "") : "Sin mensajes";
  document.querySelectorAll("[data-cmd]").forEach(x => { if (!x.dataset.enviando) x.disabled = !vivo; });

  const w = s.wifi || {}, f = [];
  f.push(["Estado", vivo ? "En línea" : dispositivo ? "Desconectado" : "Sin vincular", dispositivo ? vivo : undefined]);
  if (s.ts) {
    f.push(["Wi-Fi", w.ssid || "—"], ["Señal", w.rssi != null ? w.rssi + " dBm (" + calidad(w.rssi) + ")" : "—"]);
    f.push(["IP local", w.ip ? w.ip + "  ·  raulcito.local" : "—"]);
    f.push(["SinricPro (Alexa)", s.sinric_cfg ? (s.sinric ? "Conectado" : "Desconectado") : "Sin configurar", s.sinric_cfg ? !!s.sinric : undefined]);
    f.push(["Encendido hace", s.uptime != null ? seg(s.uptime) : "—"]);
  }
  const cx = $("conex"); cx.textContent = "";
  f.forEach(([k, v, ok]) => {
    const d = el("div", "fila"), val = el("span");
    d.appendChild(el("span", "", k));
    if (ok === undefined) val.textContent = v; else { const t = el("span"); tag(t, ok, v, v); val.appendChild(t); }
    d.appendChild(val); cx.appendChild(d);
  });
  $("cSecret").placeholder = $("cKey").placeholder = s.sinric_cfg ? "Guardada en el robot (vacío = conservar)" : "";
}
setInterval(pintarEstado, 1000);   // "hace X s" y detección de desconexión

function pintarEventos(v) {
  const ul = $("evs"); ul.textContent = "";
  const lista = Object.values(v || {}).filter(e => e && e.ts).sort((a, b) => b.ts - a.ts);
  if (!lista.length) { ul.appendChild(el("li", "nota", "Sin eventos todavía")); return; }
  const hoy = new Date().toDateString();
  lista.forEach(e => {
    const d = new Date(e.ts), li = el("li"), t = el("span");
    const hora = d.toLocaleTimeString("es-MX", { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    li.appendChild(el("time", "", d.toDateString() === hoy ? hora : d.toLocaleDateString("es-MX", { day: "numeric", month: "short" }) + " " + hora.slice(0, 5)));
    t.appendChild(el("b", "", e.tipo)); t.append(" " + (e.valor || ""));
    li.appendChild(t); ul.appendChild(li);
  });
}

// ---------- Comandos ----------
async function enviarComando(cmd, texto = cmd, esManual = false) {
  if (!enLinea()) return toast("El robot está desconectado.");
  const n = Date.now();
  const respuesta = new Promise(res => {
    esperando.set(n, res);
    setTimeout(() => { if (esperando.delete(n)) res(null); }, esManual ? 3000 : 8000);
  });
  try {
    await set(ref(db, "robot/ordenes/comando"), { cmd, n, por: usuario.email, ts: serverTimestamp() });
    const r = await respuesta;
    if (!esManual) {
      toast(!r ? "El robot no respondió." : r.ok ? "Listo: " + texto : r.error || "Error");
    } else if (r && !r.ok) {
      toast(r.error || "Error");
    }
  } catch (x) {
    esperando.delete(n);
    toast("No se pudo enviar: " + mensaje(x));
  }
}

document.querySelectorAll("[data-cmd]").forEach(b => b.onclick = async () => {
  const esManual = !!b.dataset.manual;
  const cmd = b.dataset.cmd, texto = b.textContent;
  if (!esManual) {
    b.disabled = true; b.dataset.enviando = "1";
  }
  try {
    await enviarComando(cmd, texto, esManual);
  } finally {
    if (!esManual) {
      delete b.dataset.enviando;
      b.disabled = !enLinea();
    }
  }
});

// Mantener un control de articulación genera pasos pequeños; al soltarlo se manda STOP.
let botonMantener = null, intervaloMantener = 0, envioMantener = false;
function mandarPasoMantenido() {
  if (!botonMantener || envioMantener) return;
  envioMantener = true;
  enviarComando(botonMantener.dataset.holdCmd, botonMantener.textContent.trim(), true)
    .finally(() => { envioMantener = false; });
}
function iniciarMovimientoMantenido(b, e) {
  if (!enLinea()) return toast("El robot está desconectado.");
  e.preventDefault();
  if (botonMantener) return;
  botonMantener = b;
  try { b.setPointerCapture(e.pointerId); } catch (_) {}
  mandarPasoMantenido();
  intervaloMantener = setInterval(mandarPasoMantenido, 320);
}
function terminarMovimientoMantenido() {
  if (!botonMantener) return;
  clearInterval(intervaloMantener);
  intervaloMantener = 0;
  botonMantener = null;
  enviarComando("STOP", "Paro", true);
}
document.querySelectorAll("[data-hold-cmd]").forEach(b => {
  b.addEventListener("pointerdown", e => iniciarMovimientoMantenido(b, e));
  b.addEventListener("pointerup", terminarMovimientoMantenido);
  b.addEventListener("pointercancel", terminarMovimientoMantenido);
  b.addEventListener("lostpointercapture", terminarMovimientoMantenido);
});
window.addEventListener("blur", terminarMovimientoMantenido);
document.addEventListener("visibilitychange", () => {
  if (document.hidden) terminarMovimientoMantenido();
});

const pinzaAngulo = $("pinzaAngulo"), pinzaGrados = $("pinzaGrados");
pinzaAngulo.addEventListener("input", () => { pinzaGrados.value = pinzaAngulo.value + "°"; });
$("aplicarPinza").addEventListener("click", () => {
  const grados = Number(pinzaAngulo.value);
  enviarComando(`PINZA_POS_${grados}`, `Pinza ${grados}°`, true);
});

// ---------- Vinculación (administradores) ----------
function pintarVinculacion() {
  if (!panelListo) return;
  const pend = Object.entries(solicitudes).filter(([uid]) => uid !== dispositivo).sort((a, b) => b[1].ts - a[1].ts);
  const mostrar = esAdmin ? (!dispositivo || pend.length > 0) : !dispositivo;
  ver("cVincular", mostrar);
  ver("zDesvincular", esAdmin && !!dispositivo);
  if (!mostrar) return;
  $("vinNota").textContent = !esAdmin
    ? "Todavía no hay un robot vinculado. Pide a un administrador que lo vincule."
    : !dispositivo
      ? (pend.length ? "Revisa que el código coincida con el que muestra el robot (monitor serie o http://raulcito.local) y vincúlalo."
                     : "Enciende el robot conectado a internet. Aparecerá aquí con un código de 6 dígitos.")
      : "Un robot nuevo pide vincularse. Vincularlo reemplaza al robot actual.";
  const box = $("solicitudes"); box.textContent = "";
  pend.forEach(([uid, s]) => {
    const d = el("div", "solicitud"), info = el("div"), acc = el("div", "acciones");
    info.append(el("span", "nota", "Código "), el("b", "", s.codigo), el("div", "nota", "hace " + seg((ahora() - s.ts) / 1000)));
    const bV = el("button", "chico", "Vincular"), bD = el("button", "alt chico", "Descartar");
    bV.type = bD.type = "button";
    bV.onclick = async () => {
      if (!confirm("¿Vincular el robot con código " + s.codigo + "?")) return;
      try { await update(ref(db), { dispositivo: uid, ["solicitudes/" + uid]: null }); toast("Robot vinculado. Se conecta en unos segundos."); }
      catch (x) { toast(mensaje(x)); }
    };
    bD.onclick = () => remove(ref(db, "solicitudes/" + uid)).catch(x => toast(mensaje(x)));
    acc.append(bV, bD); d.append(info, acc); box.appendChild(d);
  });
}

$("bDesvincular").onclick = async () => {
  if (!confirm("¿Desvincular el robot? Dejará de enviar datos a esta página hasta que lo vuelvas a vincular.")) return;
  try { await remove(ref(db, "dispositivo")); toast("Robot desvinculado."); }
  catch (x) { toast(mensaje(x)); }
};

// ---------- Configuración ----------
const CAMPOS = { cNombre: "nombre", cUrl: "url", cIdRobot: "idRobot", cIdPir: "idPir", cIdLdr: "idLdr" };
let configEditada = false;
Object.keys(CAMPOS).forEach(id => $(id).addEventListener("input", () => { configEditada = true; }));

function llenarConfig(c) {
  if (configEditada) return;   // no pisar lo que la persona está escribiendo
  Object.entries(CAMPOS).forEach(([id, k]) => { $(id).value = c[k] || ""; });
}

function validarConfig(d) {
  if (!d.nombre || d.nombre.length > 32) return "El nombre del robot debe tener de 1 a 32 caracteres.";
  if (d.url && !/^https?:\/\//i.test(d.url)) return "La URL debe empezar con http:// o https://";
  if (d.key && !/^[^-]{8}-[^-]{4}-[^-]{4}-[^-]{4}-[^-]{12}$/.test(d.key)) return "APP_KEY inválida: son 36 caracteres (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).";
  if (d.secret && d.secret.length !== 73) return "APP_SECRET inválido: son 73 caracteres.";
  if (!!d.key !== !!d.secret) return "Escribe APP_KEY y APP_SECRET juntos.";
  for (const [k, n] of [["idRobot", "Raulcito"], ["idPir", "Movimiento"], ["idLdr", "Oscuridad"]])
    if (d[k] && !/^[0-9a-f]{24}$/i.test(d[k])) return "El Device ID de " + n + " debe tener 24 caracteres hexadecimales.";
  return "";
}

$("fCfg").onsubmit = async e => {
  e.preventDefault();
  const v = id => $(id).value.trim();
  const d = { nombre: v("cNombre") || "raulcito-01", url: v("cUrl"), idRobot: v("cIdRobot"), idPir: v("cIdPir"), idLdr: v("cIdLdr"), key: v("cKey"), secret: v("cSecret") };
  const err = validarConfig(d);
  $("cErr").textContent = err;
  if (err) return;
  const cambios = {
    "config/nombre": d.nombre, "config/url": d.url,
    "config/idRobot": d.idRobot, "config/idPir": d.idPir, "config/idLdr": d.idLdr
  };
  if (d.key) { cambios["secretos/appKey"] = d.key; cambios["secretos/appSecret"] = d.secret; }
  $("bGuardar").disabled = true;
  try {
    await update(ref(db, "robot/ordenes"), cambios);
    $("cKey").value = $("cSecret").value = "";
    configEditada = false;
    toast(enLinea() ? "Guardado. El robot aplica los cambios y se reinicia." : "Guardado. Se aplicará cuando el robot se conecte.");
  } catch (x) { $("cErr").textContent = mensaje(x); }
  finally { $("bGuardar").disabled = false; }
};

// ---------- Equipo ----------
function pintarEquipo() {
  const ul = $("lEquipo"); ul.textContent = "";
  const yo = clave(usuario.email);
  Object.entries(equipo).sort((a, b) => (b[1].admin - a[1].admin) || a[1].email.localeCompare(b[1].email)).forEach(([k, m]) => {
    const li = el("li"), izq = el("div");
    izq.append(el("span", "correo", m.email + (k === yo ? " (tú)" : "")), " ");
    const t = el("span"); t.className = "tag" + (m.admin ? " si" : ""); t.textContent = m.admin ? "Admin" : "Miembro";
    izq.appendChild(t); li.appendChild(izq);
    if (esAdmin && k !== yo) {
      const acc = el("div", "acciones");
      const bA = el("button", "alt chico", m.admin ? "Quitar admin" : "Hacer admin");
      const bQ = el("button", "peligro chico", "Quitar");
      bA.type = bQ.type = "button";
      bA.onclick = () => set(ref(db, "equipo/" + k), { email: m.email, admin: !m.admin }).catch(x => toast(mensaje(x)));
      bQ.onclick = () => { if (confirm("¿Quitar a " + m.email + " del equipo?")) remove(ref(db, "equipo/" + k)).catch(x => toast(mensaje(x))); };
      acc.append(bA, bQ); li.appendChild(acc);
    }
    ul.appendChild(li);
  });
}

$("fEquipo").onsubmit = async e => {
  e.preventDefault();
  const correo = $("eCorreo").value.trim().toLowerCase();
  if (!/^[^\s@#$\[\]\/]+@[^\s@#$\[\]\/]+\.[^\s@#$\[\]\/]+$/.test(correo)) { $("eErr").textContent = "Escribe un correo válido."; return; }
  $("eErr").textContent = "";
  try {
    await set(ref(db, "equipo/" + clave(correo)), { email: correo, admin: $("eAdmin").checked });
    $("eCorreo").value = ""; $("eAdmin").checked = false;
    toast("Agregado. Ya puede crear su cuenta con ese correo.");
  } catch (x) { $("eErr").textContent = mensaje(x); }
};
