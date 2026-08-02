// ============================================================
//  pagina_web.h  —  HTML/CSS/JS de la estación meteorológica
//  Se incluye desde el .ino con:  #include "pagina_web.h"
// ============================================================
//
//  El HTML se almacena como array de bytes en Flash (PROGMEM).
//  Separarlo en un .h evita que el preprocesador de Arduino
//  interprete palabras reservadas de JS ("function", "const",
//  "async", etc.) como código C++.
// ============================================================

#ifndef PAGINA_WEB_H
#define PAGINA_WEB_H

#include <pgmspace.h>

static const char HTML_PAGE[] PROGMEM = R"===(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>estacion meteo</title>
<link href="https://fonts.googleapis.com/css2?family=Source+Code+Pro:wght@300;400;500;600&display=swap" rel="stylesheet">
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{background:#f5f5f2;color:#111;font-family:'Source Code Pro',monospace;line-height:1.7;font-size:15px;}
.container{width:100%;max-width:700px;margin:auto;padding:70px 30px;}
.logo{font-size:2rem;line-height:1;margin-bottom:40px;font-weight:600;}
.subtitle{color:#666;font-size:.85rem;margin-top:6px;font-weight:300;}
nav{margin-bottom:70px;display:flex;flex-wrap:wrap;align-items:center;}
.status-dot{display:inline-block;width:7px;height:7px;background:#2c55ff;border-radius:50%;margin-right:8px;animation:blink 2s infinite;}
@keyframes blink{0%,100%{opacity:1;}50%{opacity:.25;}}
.ts{color:#666;font-size:.8rem;margin-left:auto;}
section{margin-bottom:70px;}
h2{font-size:1rem;margin-bottom:25px;font-weight:600;}
.alerta-container{margin-bottom:40px;}
.alerta{padding:14px 18px;margin-bottom:10px;font-size:.875rem;border-left:3px solid;display:none;}
.alerta.visible{display:block;}
.alerta-roja{border-color:#c0392b;background:#fdf0ee;color:#c0392b;}
.alerta-naranja{border-color:#e67e22;background:#fef6ee;color:#d35400;}
.alerta-amarilla{border-color:#f39c12;background:#fefcee;color:#b7770d;}
.alerta-verde{border-color:#27ae60;background:#eefaf3;color:#1e8449;}
.alerta-titulo{font-weight:600;display:block;margin-bottom:2px;}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:0;}
.dato{padding:16px 0;border-bottom:1px solid #ddd;}
.dato:nth-child(odd){padding-right:20px;border-right:1px solid #ddd;}
.dato:nth-child(even){padding-left:20px;}
.dato-label{color:#666;font-size:.8rem;font-weight:400;display:block;margin-bottom:3px;}
.dato-valor{font-size:1.4rem;font-weight:600;line-height:1.1;}
.dato-unidad{font-size:.75rem;color:#888;font-weight:300;margin-left:3px;}
.nivel-ok{color:#111;}
.nivel-aviso{color:#e67e22;}
.nivel-alto{color:#c0392b;}
.nivel-bajo{color:#2c55ff;}
.lluvia-bar-wrap{margin:10px 0 4px;background:#e0e0dc;height:4px;width:100%;}
.lluvia-bar{height:4px;background:#2c55ff;transition:width .6s ease;}
.warming{display:inline-block;font-size:.8rem;color:#e67e22;border:1px solid #e67e22;padding:2px 8px;margin-left:8px;}
.footer{color:#777;font-size:.85rem;margin-top:80px;border-top:1px solid #ddd;padding-top:20px;}
.footer a{color:#2c55ff;text-decoration:none;}
@media(max-width:500px){
  .container{padding:50px 20px;}
  .logo{font-size:1.6rem;}
  .grid{grid-template-columns:1fr;}
  .dato:nth-child(odd){border-right:none;padding-right:0;}
  .dato:nth-child(even){padding-left:0;}
}
</style>
</head>
<body>
<div class="container">

  <div class="logo">
    estacion<br>meteorológica<br>lucilista
    <div class="subtitle">ESP32 &middot; Access Point</div>
  </div>

  <nav>
    <span class="status-dot"></span>
    <span style="color:#666;font-size:.85rem;">en vivo &middot; actualiza cada 10 s</span>
    <span class="ts" id="ts">--:--:--</span>
  </nav>

  <div class="alerta-container" id="alertas"></div>

  <section id="clima">
    <h2>clima</h2>
    <div class="grid">
      <div class="dato">
        <span class="dato-label">temperatura</span>
        <span class="dato-valor nivel-ok" id="temp">--</span>
        <span class="dato-unidad">&#176;C</span>
      </div>
      <div class="dato">
        <span class="dato-label">humedad</span>
        <span class="dato-valor nivel-ok" id="hum">--</span>
        <span class="dato-unidad">%</span>
      </div>
      <div class="dato">
        <span class="dato-label">presion</span>
        <span class="dato-valor" id="pres">--</span>
        <span class="dato-unidad">hPa</span>
      </div>
      <div class="dato">
        <span class="dato-label">altitud aprox.</span>
        <span class="dato-valor" id="alt">--</span>
        <span class="dato-unidad">m</span>
      </div>
    </div>
  </section>

  <section id="luz">
    <h2>luz &middot; uv</h2>
    <div class="grid">
      <div class="dato">
        <span class="dato-label">luz visible</span>
        <span class="dato-valor" id="lux">--</span>
        <span class="dato-unidad">lux</span>
      </div>
      <div class="dato">
        <span class="dato-label">indice UV (OMS)</span>
        <span class="dato-valor nivel-ok" id="uvi">--</span>
      </div>
    </div>
  </section>

  <section id="aire">
    <h2>calidad de aire <span class="warming" id="warming" style="display:none">calentando...</span></h2>
    <div class="grid">
      <div class="dato">
        <span class="dato-label">eCO2</span>
        <span class="dato-valor nivel-ok" id="eco2">--</span>
        <span class="dato-unidad">ppm</span>
      </div>
      <div class="dato">
        <span class="dato-label">TVOC</span>
        <span class="dato-valor nivel-ok" id="tvoc">--</span>
        <span class="dato-unidad">ppb</span>
      </div>
    </div>
  </section>

  <section id="lluvia">
    <h2>precipitacion</h2>
    <div class="grid">
      <div class="dato">
        <span class="dato-label">acumulado total</span>
        <span class="dato-valor" id="mm">--</span>
        <span class="dato-unidad">mm</span>
      </div>
      <div class="dato">
        <span class="dato-label">tasa ~1h equiv.</span>
        <span class="dato-valor nivel-ok" id="mmh">--</span>
        <span class="dato-unidad">mm/h</span>
      </div>
    </div>
    <div class="lluvia-bar-wrap">
      <div class="lluvia-bar" id="lluviaBar" style="width:0%"></div>
    </div>
    <span style="color:#888;font-size:.8rem;" id="pulsos">0 vuelcos de cubeta</span>
  </section>

  <div class="footer">
    estacion meteorológica &middot; 11c l.a.g
  </div>

</div>

<script>
var UMBRALES = {
  temp:  { bajo: 10, aviso: 30, alto: 38 },
  hum:   { bajo: 20, aviso: 80, alto: 90 },
  uvi:   { aviso: 6, alto: 8, extremo: 11 },
  eco2:  { aviso: 1000, alto: 2000 },
  tvoc:  { aviso: 500,  alto: 2000 },
  mmh:   { aviso: 10, alto: 30, extremo: 50 }
};

function getNivel(val, u) {
  if (!u) return "ok";
  if (u.extremo !== undefined && val >= u.extremo) return "extremo";
  if (u.alto    !== undefined && val >= u.alto)    return "alto";
  if (u.aviso   !== undefined && val >= u.aviso)   return "aviso";
  if (u.bajo    !== undefined && val <= u.bajo)    return "bajo";
  return "ok";
}

function claseNivel(n) {
  if (n === "extremo" || n === "alto") return "nivel-alto";
  if (n === "aviso")                   return "nivel-aviso";
  if (n === "bajo")                    return "nivel-bajo";
  return "nivel-ok";
}

function fmtNum(val, dec) {
  if (val === null || val === undefined) return "--";
  return Number(val).toFixed(dec !== undefined ? dec : 1);
}

function setEl(id, val, dec) {
  var el = document.getElementById(id);
  if (el) el.textContent = fmtNum(val, dec);
}

function setColor(id, val, umbral) {
  var el = document.getElementById(id);
  if (!el) return;
  el.className = el.className.replace(/nivel-\w+/g, "");
  el.classList.add(claseNivel(getNivel(val, umbral)));
}

function generarAlertas(d) {
  var msgs = [];
  var nT = getNivel(d.temperatura, UMBRALES.temp);
  if (nT === "alto")  msgs.push({ c: "alerta-roja",    t: "temperatura alta",     m: fmtNum(d.temperatura) + " C - calor intenso, evita exposicion prolongada." });
  if (nT === "aviso") msgs.push({ c: "alerta-naranja", t: "temperatura elevada",  m: fmtNum(d.temperatura) + " C - mantente hidratado." });
  if (nT === "bajo")  msgs.push({ c: "alerta-amarilla",t: "temperatura baja",     m: fmtNum(d.temperatura) + " C - considera abrigo al salir." });

  var nU = getNivel(d.uvi, UMBRALES.uvi);
  if (nU === "extremo") msgs.push({ c: "alerta-roja",    t: "radiacion UV extrema", m: "IUV " + fmtNum(d.uvi, 1) + " - permanece en interiores o usa proteccion maxima (SPF 50+)." });
  if (nU === "alto")    msgs.push({ c: "alerta-naranja", t: "radiacion UV alta",    m: "IUV " + fmtNum(d.uvi, 1) + " - protector solar y lentes recomendados." });
  if (nU === "aviso")   msgs.push({ c: "alerta-amarilla",t: "radiacion UV moderada",m: "IUV " + fmtNum(d.uvi, 1) + " - precaucion al mediodia." });

  var nC = getNivel(d.eco2, UMBRALES.eco2);
  if (nC === "alto")  msgs.push({ c: "alerta-roja",    t: "CO2 muy elevado", m: fmtNum(d.eco2, 0) + " ppm - ventila el espacio inmediatamente." });
  if (nC === "aviso") msgs.push({ c: "alerta-naranja", t: "CO2 elevado",     m: fmtNum(d.eco2, 0) + " ppm - ventilacion recomendada." });

  var nV = getNivel(d.tvoc, UMBRALES.tvoc);
  if (nV === "alto")  msgs.push({ c: "alerta-roja",    t: "TVOC muy alto", m: fmtNum(d.tvoc, 0) + " ppb - posibles compuestos volatiles. Ventila o sal del espacio." });
  if (nV === "aviso") msgs.push({ c: "alerta-naranja", t: "TVOC elevado",  m: fmtNum(d.tvoc, 0) + " ppb - calidad de aire deteriorada." });

  var nL = getNivel(d.lluviaPorHora, UMBRALES.mmh);
  if (nL === "extremo") msgs.push({ c: "alerta-roja",    t: "lluvia torrencial", m: fmtNum(d.lluviaPorHora) + " mm/h - no salgas sin resguardo." });
  if (nL === "alto")    msgs.push({ c: "alerta-naranja", t: "lluvia intensa",    m: fmtNum(d.lluviaPorHora) + " mm/h - ten precaucion en exteriores." });
  if (nL === "aviso")   msgs.push({ c: "alerta-amarilla",t: "lluvia moderada",   m: fmtNum(d.lluviaPorHora) + " mm/h - lleva paraguas." });

  if (msgs.length === 0) {
    msgs.push({ c: "alerta-verde", t: "condiciones normales", m: "Todos los parametros dentro de rangos seguros." });
  }
  return msgs;
}

function renderAlertas(d) {
  var box = document.getElementById("alertas");
  var msgs = generarAlertas(d);
  var html = "";
  for (var i = 0; i < msgs.length; i++) {
    html += "<div class=\"alerta " + msgs[i].c + " visible\">" +
            "<span class=\"alerta-titulo\">" + msgs[i].t + "</span>" +
            msgs[i].m + "</div>";
  }
  box.innerHTML = html;
}

function actualizar() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/datos", true);
  xhr.onload = function() {
    if (xhr.status !== 200) return;
    var d;
    try { d = JSON.parse(xhr.responseText); } catch(e) { return; }

    var ahora = new Date();
    var hh = ("0" + ahora.getHours()).slice(-2);
    var mm = ("0" + ahora.getMinutes()).slice(-2);
    var ss = ("0" + ahora.getSeconds()).slice(-2);
    document.getElementById("ts").textContent = hh + ":" + mm + ":" + ss;

    setEl("temp", d.temperatura, 1);   setColor("temp", d.temperatura, UMBRALES.temp);
    setEl("hum",  d.humedad, 0);       setColor("hum",  d.humedad,     UMBRALES.hum);
    setEl("pres", d.presion, 1);
    setEl("alt",  d.altitud, 0);
    setEl("lux",  d.lux, 0);
    setEl("uvi",  d.uvi, 2);           setColor("uvi",  d.uvi,  UMBRALES.uvi);
    setEl("eco2", d.eco2, 0);          setColor("eco2", d.eco2, UMBRALES.eco2);
    setEl("tvoc", d.tvoc, 0);          setColor("tvoc", d.tvoc, UMBRALES.tvoc);
    setEl("mm",   d.mmAcumulados, 2);
    setEl("mmh",  d.lluviaPorHora, 2); setColor("mmh",  d.lluviaPorHora, UMBRALES.mmh);

    var pct = Math.min(d.mmAcumulados / 50 * 100, 100);
    document.getElementById("lluviaBar").style.width = pct + "%";
    document.getElementById("pulsos").textContent =
      d.pulsos + " vuelcos de cubeta  |  " +
      Number(d.horasOperacion).toFixed(2) + " h operacion";

    document.getElementById("warming").style.display =
      d.sgpCalentando ? "inline-block" : "none";

    renderAlertas(d);
  };
  xhr.onerror = function() { console.warn("Error de red"); };
  xhr.send();
}

actualizar();
setInterval(actualizar, 10000);
</script>
</body>
</html>
)===";

#endif
