#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ajuste_et_gps.py - Control de puntos GPS y ajuste de un relevamiento con
estación total (ET) a puntos GPS, en POSGAR 07 / Gauss-Krüger Argentina.

Qué hace:
  1. Analiza los puntos GPS: rango de faja, lat/lon, factores de escala,
     repeticiones, precisiones y comparación contra las coordenadas de
     partida (por ejemplo las entregadas por el comitente).
  2. Compara la llegada de la ET con el punto GPS de cierre: error de cierre,
     descomposición longitudinal/transversal y cuánto explica el factor de
     escala combinado (Gauss-Krüger + altura) y la orientación inicial.
  3. Ajusta todos los puntos de la ET a los puntos GPS de inicio y cierre:
       - helmert : transformación de semejanza 2D (giro + escala) con el
                   inicio fijo; corrige errores sistemáticos de escala y
                   orientación. Es el método por defecto.
       - bowditch: compensación proporcional a la distancia recorrida
                   (regla de la brújula); requiere el orden de la poligonal.
     Las cotas se compensan linealmente en ambos casos.
  4. Verifica el ajuste con puntos de control independientes (otros puntos
     GPS o del comitente que la ET haya medido y que no se usaron).

Uso típico:
  python ajuste_et_gps.py --gps gps.csv --et et.csv --inicio PF1 \
      --orientacion PF2 --cierre PF9 --control control.csv \
      --poligonal PF1,E1,E2,E3,PF9 --faja 2 --salida resultados

Solo usa la biblioteca estándar de Python (3.8 o superior).
"""

import argparse
import csv
import math
import os
import sys
import unicodedata

SEG = 180.0 / math.pi * 3600.0  # segundos sexagesimales por radián

# --- Elipsoide GRS80 (POSGAR 07) ---
A_ELL = 6378137.0
F_ELL = 1.0 / 298.257222101
E2 = F_ELL * (2.0 - F_ELL)
EP2 = E2 / (1.0 - E2)
K0 = 1.0


# ---------------------------------------------------------------------------
# Proyección Gauss-Krüger Argentina (EPSG 5343-5349): k0 = 1, origen de
# latitudes en el Polo Sur (falso norte 0), falso este = faja*1e6 + 500000.
# Se usa solo para factores de escala y lat/lon informativos.
# ---------------------------------------------------------------------------

def faja_params(faja):
    if not 1 <= faja <= 7:
        raise ValueError("La faja debe estar entre 1 y 7")
    return math.radians(-72.0 + 3.0 * (faja - 1)), faja * 1_000_000.0 + 500_000.0


def arco_meridiano(phi):
    e2, e4, e6 = E2, E2 ** 2, E2 ** 3
    return A_ELL * ((1 - e2 / 4 - 3 * e4 / 64 - 5 * e6 / 256) * phi
                    - (3 * e2 / 8 + 3 * e4 / 32 + 45 * e6 / 1024) * math.sin(2 * phi)
                    + (15 * e4 / 256 + 45 * e6 / 1024) * math.sin(4 * phi)
                    - (35 * e6 / 3072) * math.sin(6 * phi))


M_POLO_SUR = arco_meridiano(-math.pi / 2)


def gk_directa(lat, lon, faja):
    """lat/lon en grados -> (E, N, factor de escala puntual)."""
    lon0, fe = faja_params(faja)
    phi, lam = math.radians(lat), math.radians(lon)
    s, c, t = math.sin(phi), math.cos(phi), math.tan(phi)
    nu = A_ELL / math.sqrt(1 - E2 * s * s)
    T, C, A = t * t, EP2 * c * c, (lam - lon0) * c
    x = K0 * nu * (A + (1 - T + C) * A ** 3 / 6
                   + (5 - 18 * T + T * T + 72 * C - 58 * EP2) * A ** 5 / 120)
    y = K0 * (arco_meridiano(phi) - M_POLO_SUR + nu * t * (
        A * A / 2 + (5 - T + 9 * C + 4 * C * C) * A ** 4 / 24
        + (61 - 58 * T + T * T + 600 * C - 330 * EP2) * A ** 6 / 720))
    k = K0 * (1 + (1 + C) * A * A / 2
              + (5 - 4 * T + 42 * C + 13 * C * C - 28 * EP2) * A ** 4 / 24
              + (61 - 148 * T + 16 * T * T) * A ** 6 / 720)
    return fe + x, y, k


def gk_inversa(E, N, faja):
    """(E, N) -> lat, lon en grados."""
    lon0, fe = faja_params(faja)
    x = E - fe
    mu = (M_POLO_SUR + N / K0) / (A_ELL * (1 - E2 / 4 - 3 * E2 ** 2 / 64 - 5 * E2 ** 3 / 256))
    e1 = (1 - math.sqrt(1 - E2)) / (1 + math.sqrt(1 - E2))
    phi1 = (mu + (3 * e1 / 2 - 27 * e1 ** 3 / 32) * math.sin(2 * mu)
            + (21 * e1 ** 2 / 16 - 55 * e1 ** 4 / 32) * math.sin(4 * mu)
            + (151 * e1 ** 3 / 96) * math.sin(6 * mu)
            + (1097 * e1 ** 4 / 512) * math.sin(8 * mu))
    s, c, t = math.sin(phi1), math.cos(phi1), math.tan(phi1)
    C1, T1 = EP2 * c * c, t * t
    nu1 = A_ELL / math.sqrt(1 - E2 * s * s)
    rho1 = A_ELL * (1 - E2) / (1 - E2 * s * s) ** 1.5
    D = x / (nu1 * K0)
    phi = phi1 - (nu1 * t / rho1) * (
        D * D / 2 - (5 + 3 * T1 + 10 * C1 - 4 * C1 * C1 - 9 * EP2) * D ** 4 / 24
        + (61 + 90 * T1 + 298 * C1 + 45 * T1 * T1 - 252 * EP2 - 3 * C1 * C1) * D ** 6 / 720)
    lam = lon0 + (D - (1 + 2 * T1 + C1) * D ** 3 / 6
                  + (5 - 2 * C1 + 28 * T1 - 3 * C1 * C1 + 8 * EP2 + 24 * T1 * T1) * D ** 5 / 120) / c
    return math.degrees(phi), math.degrees(lam)


def radio_medio(lat):
    s = math.sin(math.radians(lat))
    w2 = 1 - E2 * s * s
    return math.sqrt((A_ELL / math.sqrt(w2)) * (A_ELL * (1 - E2) / w2 ** 1.5))


def factores(E, N, H, faja, ondulacion=0.0):
    """Devuelve (lat, lon, k_GK, k_altura, k_combinado) para un punto."""
    lat, lon = gk_inversa(E, N, faja)
    _, _, k = gk_directa(lat, lon, faja)
    R = radio_medio(lat)
    kh = R / (R + (H or 0.0) + ondulacion)
    return lat, lon, k, kh, k * kh


def ppm(f):
    return (f - 1.0) * 1e6


# ---------------------------------------------------------------------------
# Lectura de archivos de puntos
# ---------------------------------------------------------------------------

ALIAS = {
    "nombre": {"punto", "nombre", "name", "pt", "id", "pto", "point", "nro", "numero", "p"},
    "E": {"e", "este", "easting", "este_y", "y_gk", "ygk"},
    "N": {"n", "norte", "northing", "norte_x", "x_gk", "xgk"},
    "X": {"x"},
    "Y": {"y"},
    "H": {"h", "z", "cota", "altura", "elev", "elevacion", "height", "alt", "h_elip", "h_ort"},
    "estacion": {"estacion", "est", "station", "desde", "from", "stn", "estacion_origen"},
    "sh": {"hrms", "sh", "sigma_h", "prec_h", "qh", "hprec", "precision_h", "ph", "hsdv", "sdh"},
    "sv": {"vrms", "sv", "sigma_v", "prec_v", "qv", "vprec", "precision_v", "pv", "vsdv", "sdv"},
    "sol": {"solucion", "estado", "status", "sol", "solution", "tipo_sol", "fix"},
    "codigo": {"codigo", "code", "cod", "descripcion", "desc", "obs"},
}


def _norm(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower().strip()
    return s.replace(" ", "_").replace("(", "").replace(")", "").replace(".", "")


def _es_num(s):
    try:
        float(s.replace(",", "."))
        return True
    except ValueError:
        return False


def leer_puntos(ruta, faja, formato="PENZ", avisos=None):
    """Lee un CSV/TXT de puntos. Devuelve una lista de dicts."""
    avisos = avisos if avisos is not None else []
    with open(ruta, encoding="utf-8-sig", errors="replace") as fh:
        lineas = [l.rstrip("\r\n") for l in fh if l.strip() and not l.lstrip().startswith("#")]
    if not lineas:
        raise SystemExit(f"{ruta}: archivo vacío")
    muestra = lineas[0]
    cand = {d: muestra.count(d) for d in (";", "\t", ",")}
    delim = max(cand, key=cand.get) if max(cand.values()) > 0 else None
    if delim is None:
        filas = [l.split() for l in lineas]
    else:
        filas = list(csv.reader(lineas, delimiter=delim))
    filas = [[c.strip() for c in f] for f in filas]
    if delim != ",":
        filas = [[c.replace(",", ".") if _es_num(c) else c for c in f] for f in filas]

    cab = filas[0]
    hay_cabecera = len(cab) < 3 or not all(_es_num(c) for c in cab[1:4])
    col = {}
    if hay_cabecera:
        for i, nombre in enumerate(cab):
            n = _norm(nombre)
            for clave, alias in ALIAS.items():
                if n in alias and clave not in col:
                    col[clave] = i
        filas = filas[1:]
        if "E" not in col and "X" in col:
            col["E"] = col.pop("X")
        if "N" not in col and "Y" in col:
            col["N"] = col.pop("Y")
        faltan = [c for c in ("nombre", "E", "N") if c not in col]
        if faltan:
            raise SystemExit(f"{ruta}: no encuentro las columnas {faltan} en la cabecera {cab}")
    else:
        orden = formato.upper()
        mapa = {"P": "nombre", "E": "E", "N": "N", "Z": "H", "H": "H"}
        for i, ch in enumerate(orden):
            if ch in mapa:
                col[mapa[ch]] = i
        if len(cab) > len(orden):
            col["codigo"] = len(orden)

    pts = []
    for nl, f in enumerate(filas, start=2 if hay_cabecera else 1):
        try:
            p = {
                "nombre": f[col["nombre"]],
                "E": float(f[col["E"]]),
                "N": float(f[col["N"]]),
                "H": float(f[col["H"]]) if "H" in col and col["H"] < len(f) and f[col["H"]] else None,
            }
        except (ValueError, IndexError):
            avisos.append(f"{os.path.basename(ruta)}: línea {nl} ignorada ({' '.join(f)})")
            continue
        for extra in ("estacion", "sh", "sv", "sol", "codigo"):
            if extra in col and col[extra] < len(f):
                v = f[col[extra]]
                if extra in ("sh", "sv"):
                    v = float(v) if _es_num(v) else None
                p[extra] = v
        pts.append(p)

    # Verificación de magnitudes: E ~ faja*1e6+500000, N ~ 3.5e6 .. 8e6
    _, fe = faja_params(faja)
    es_e = lambda v: abs(v - fe) < 400_000
    es_n = lambda v: 3_500_000 < v < 8_000_000
    if pts and all(es_n(p["E"]) and es_e(p["N"]) for p in pts):
        for p in pts:
            p["E"], p["N"] = p["N"], p["E"]
        avisos.append(f"{os.path.basename(ruta)}: las columnas Este/Norte venían invertidas "
                      "(convención GK X=Norte, Y=Este); se corrigieron automáticamente")
    malos = [p["nombre"] for p in pts if not (es_e(p["E"]) and es_n(p["N"]))]
    if malos:
        avisos.append(f"{os.path.basename(ruta)}: {len(malos)} punto(s) fuera del rango esperado "
                      f"para la faja {faja}: {', '.join(malos[:10])}")
    return pts


# ---------------------------------------------------------------------------
# Utilidades
# ---------------------------------------------------------------------------

def dist(p, q):
    return math.hypot(q["E"] - p["E"], q["N"] - p["N"])


def acimut(p, q):
    return math.atan2(q["E"] - p["E"], q["N"] - p["N"]) % (2 * math.pi)


def dms(rad):
    g = math.degrees(rad) % 360.0
    d = int(g)
    m = int((g - d) * 60)
    s = ((g - d) * 60 - m) * 60
    if s >= 59.995:
        s, m = 0.0, m + 1
    if m == 60:
        m, d = 0, d + 1
    return f"{d:3d}°{m:02d}'{s:05.2f}\""


def ang_dif(a, b):
    """a - b normalizado a (-pi, pi]."""
    d = (a - b) % (2 * math.pi)
    return d - 2 * math.pi if d > math.pi else d


def media(v):
    return sum(v) / len(v)


def desvio(v):
    if len(v) < 2:
        return 0.0
    m = media(v)
    return math.sqrt(sum((x - m) ** 2 for x in v) / (len(v) - 1))


def f3(v, d=3):
    return "-" if v is None else f"{v:.{d}f}"


def fs(v, d=3):
    return "-" if v is None else f"{v:+.{d}f}"


class Reporte:
    def __init__(self):
        self.lineas = []

    def __call__(self, s=""):
        print(s)
        self.lineas.append(s)

    def titulo(self, s):
        self("")
        self("=" * 78)
        self(s)
        self("=" * 78)

    def sub(self, s):
        self("")
        self(s)
        self("-" * len(s))

    def tabla(self, cab, filas):
        filas = [[str(c) for c in f] for f in filas]
        anchos = [max(len(str(cab[i])), *(len(f[i]) for f in filas)) if filas else len(str(cab[i]))
                  for i in range(len(cab))]
        self("  ".join(str(c).rjust(anchos[i]) for i, c in enumerate(cab)))
        self("  ".join("-" * a for a in anchos))
        for f in filas:
            self("  ".join(c.rjust(anchos[i]) for i, c in enumerate(f)))

    def guardar(self, ruta):
        with open(ruta, "w", encoding="utf-8") as fh:
            fh.write("\n".join(self.lineas) + "\n")


def promediar_repetidos(pts):
    grupos = {}
    for p in pts:
        grupos.setdefault(p["nombre"], []).append(p)
    unicos, reps = {}, []
    for nombre, g in grupos.items():
        q = dict(g[0])
        if len(g) > 1:
            q["E"] = media([x["E"] for x in g])
            q["N"] = media([x["N"] for x in g])
            hs = [x["H"] for x in g if x["H"] is not None]
            q["H"] = media(hs) if hs else None
            reps.append((nombre, len(g),
                         max(x["E"] for x in g) - min(x["E"] for x in g),
                         max(x["N"] for x in g) - min(x["N"] for x in g),
                         (max(hs) - min(hs)) if hs else None))
        unicos[nombre] = q
    return unicos, reps


def helmert_ajustar(pares):
    """Semejanza 2D por mínimos cuadrados. pares = [(origen, destino), ...]
    con dicts que tienen E y N. Devuelve dict con parámetros y residuos."""
    n = len(pares)
    xc = media([o["E"] for o, _ in pares])
    yc = media([o["N"] for o, _ in pares])
    Xc = media([d["E"] for _, d in pares])
    Yc = media([d["N"] for _, d in pares])
    sxx = sa = sb = 0.0
    for o, d in pares:
        dx, dy = o["E"] - xc, o["N"] - yc
        dX, dY = d["E"] - Xc, d["N"] - Yc
        sxx += dx * dx + dy * dy
        sa += dx * dX + dy * dY
        sb += dx * dY - dy * dX
    a, b = sa / sxx, sb / sxx
    T = {"a": a, "b": b, "xc": xc, "yc": yc, "Xc": Xc, "Yc": Yc,
         "escala": math.hypot(a, b), "giro": math.atan2(b, a), "n": n}
    res = []
    for o, d in pares:
        E, N = helmert_aplicar(T, o["E"], o["N"])
        res.append((o["nombre"], E - d["E"], N - d["N"]))
    T["residuos"] = res
    T["s0"] = (math.sqrt(sum(r[1] ** 2 + r[2] ** 2 for r in res) / (2 * n - 4))
               if n > 2 else None)
    return T


def helmert_aplicar(T, E, N):
    dx, dy = E - T["xc"], N - T["yc"]
    return T["Xc"] + T["a"] * dx - T["b"] * dy, T["Yc"] + T["b"] * dx + T["a"] * dy


def par_nombres(arg):
    """'ET=GPS' -> ('ET', 'GPS'); 'P' -> ('P', 'P')."""
    if arg is None:
        return None
    if "=" in arg:
        a, b = arg.split("=", 1)
        return a.strip(), b.strip()
    return arg.strip(), arg.strip()


# ---------------------------------------------------------------------------
# 1. Análisis de los puntos GPS
# ---------------------------------------------------------------------------

def analizar_gps(R, gps, reps, control, args):
    R.titulo("1. ANÁLISIS DE LOS PUNTOS GPS (POSGAR 07 - faja %d)" % args.faja)

    R.sub("1.1 Puntos, ubicación y factores de escala")
    filas, kcs = [], []
    for p in gps.values():
        lat, lon, k, kh, kc = factores(p["E"], p["N"], p["H"], args.faja, args.ondulacion)
        kcs.append(kc)
        p["kc"] = kc
        filas.append([p["nombre"], f3(p["E"]), f3(p["N"]), f3(p["H"]),
                      f"{lat:.6f}", f"{lon:.6f}", f"{ppm(k):+.1f}", f"{ppm(kh):+.1f}", f"{ppm(kc):+.1f}"])
    R.tabla(["Punto", "Este", "Norte", "H", "Lat", "Lon", "kGK ppm", "kH ppm", "kComb ppm"], filas)
    if kcs:
        R("")
        R(f"Factor combinado medio de la zona: {media(kcs):.8f} ({ppm(media(kcs)):+.1f} ppm)")
        R("  -> una distancia de terreno de 1000 m mide %.3f m en la cuadrícula GK."
          % (1000 * media(kcs)))
        if args.ondulacion == 0:
            R("  (Si las alturas son ortométricas, el factor de altura cambia ~0,16 ppm por cada"
              " metro de ondulación; con --ondulacion se puede indicar. Es despreciable.)")

    if reps:
        R.sub("1.2 Puntos medidos más de una vez (se usa el promedio)")
        filas = []
        for nombre, n, rE, rN, rH in reps:
            mal = rE > args.tol_h or rN > args.tol_h or (rH is not None and rH > args.tol_v)
            filas.append([nombre, n, f3(rE), f3(rN), f3(rH), "REVISAR" if mal else "ok"])
        R.tabla(["Punto", "Veces", "Rango E", "Rango N", "Rango H", "Estado"], filas)

    prec = [p for p in gps.values() if p.get("sh") is not None or p.get("sol")]
    if prec:
        R.sub("1.3 Precisión informada por el receptor")
        filas = []
        for p in prec:
            sol = (p.get("sol") or "").lower()
            mal_sol = any(k in sol for k in ("float", "flot", "aut", "dgps", "single", "simple"))
            mal = mal_sol or (p.get("sh") or 0) > args.tol_h or (p.get("sv") or 0) > args.tol_v
            filas.append([p["nombre"], f3(p.get("sh")), f3(p.get("sv")), p.get("sol") or "-",
                          "REVISAR" if mal else "ok"])
        R.tabla(["Punto", "Prec H", "Prec V", "Solución", "Estado"], filas)

    if not control:
        R("")
        R("(No se indicó archivo --control con las coordenadas de partida: no se comparan.)")
        return

    comunes = [n for n in control if n in gps]
    R.sub("1.4 GPS propio vs. coordenadas de partida (%d puntos en común)" % len(comunes))
    if not comunes:
        R("No hay puntos con el mismo nombre en ambos archivos.")
        return
    filas, dEs, dNs, dHs = [], [], [], []
    for n in comunes:
        c, g = control[n], gps[n]
        dE, dN = g["E"] - c["E"], g["N"] - c["N"]
        dH = (g["H"] - c["H"]) if g["H"] is not None and c["H"] is not None else None
        dEs.append(dE)
        dNs.append(dN)
        if dH is not None:
            dHs.append(dH)
        mal = math.hypot(dE, dN) > args.tol_h or (dH is not None and abs(dH) > args.tol_v)
        filas.append([n, fs(dE), fs(dN), f3(math.hypot(dE, dN)), fs(dH), "REVISAR" if mal else "ok"])
    R.tabla(["Punto", "dE", "dN", "dPlan", "dH", "Estado"], filas)
    R("(d = GPS propio - partida; tolerancias: plani %.3f m, alti %.3f m)" % (args.tol_h, args.tol_v))

    mE, mN = media(dEs), media(dNs)
    R("")
    R(f"Diferencia media: dE {mE:+.3f}  dN {mN:+.3f}  (plani {math.hypot(mE, mN):.3f} m)"
      + (f"  dH {media(dHs):+.3f}" if dHs else ""))
    if len(comunes) >= 2:
        dispE, dispN = desvio(dEs), desvio(dNs)
        disp = math.hypot(dispE, dispN)
        R(f"Dispersión alrededor de la media: sE {dispE:.3f}  sN {dispN:.3f}"
          + (f"  sH {desvio(dHs):.3f}" if len(dHs) >= 2 else ""))
        if math.hypot(mE, mN) > args.tol_h and disp < args.tol_h:
            R("=> Hay un CORRIMIENTO SISTEMÁTICO (igual en todos los puntos). Suele indicar base")
            R("   cargada con otra coordenada, otro marco/época que los datos de partida, o base")
            R("   'aquí' (autónoma). No es un error de un punto: revisar la coordenada de la base.")
        elif disp > args.tol_h:
            R("=> Las diferencias NO son uniformes: algún punto (de partida o propio) está mal.")
            R("   Ver la comparación de distancias para ubicarlo.")
        else:
            R("=> Coinciden dentro de tolerancia.")
    if dHs and abs(media(dHs)) > 5:
        R("=> Diferencia de cota de %.2f m: probablemente un juego de alturas es elipsoidal y el"
          % media(dHs))
        R("   otro ortométrico (ondulación del geoide). No mezclar ambos en el ajuste.")

    if len(comunes) >= 2:
        R.sub("1.5 Distancias entre puntos: GPS propio vs. partida (independiente de corrimientos)")
        filas, malos = [], {}
        for i in range(len(comunes)):
            for j in range(i + 1, len(comunes)):
                a, b = comunes[i], comunes[j]
                dc, dg = dist(control[a], control[b]), dist(gps[a], gps[b])
                ddh = None
                if None not in (control[a]["H"], control[b]["H"], gps[a]["H"], gps[b]["H"]):
                    ddh = (gps[b]["H"] - gps[a]["H"]) - (control[b]["H"] - control[a]["H"])
                mal = abs(dg - dc) > math.sqrt(2) * args.tol_h or (
                    ddh is not None and abs(ddh) > math.sqrt(2) * args.tol_v)
                if mal:
                    malos[a] = malos.get(a, 0) + 1
                    malos[b] = malos.get(b, 0) + 1
                filas.append([f"{a}-{b}", f3(dc), f3(dg), fs(dg - dc), f"{(dg - dc) / dc * 1e6:+.0f}",
                              fs(ddh), "REVISAR" if mal else "ok"])
        R.tabla(["Tramo", "D partida", "D GPS", "Dif", "ppm", "Dif dH", "Estado"], filas)
        if malos:
            peor = max(malos, key=malos.get)
            R("")
            R(f"Tramos fuera de tolerancia por punto: {malos}")
            if malos[peor] >= max(2, len(comunes) - 1):
                R(f"=> El punto sospechoso es {peor} (falla con casi todos los demás).")

    if len(comunes) >= 3:
        R.sub("1.6 Transformación de semejanza partida -> GPS (detecta puntos que no ajustan)")
        T = helmert_ajustar([(control[n], gps[n]) for n in comunes])
        R(f"Escala {ppm(T['escala']):+.1f} ppm   Giro {T['giro'] * SEG:+.2f}\"   s0 = {T['s0']:.3f} m")
        R.tabla(["Punto", "vE", "vN", "vPlan"],
                [[n, fs(vE), fs(vN), f3(math.hypot(vE, vN))] for n, vE, vN in T["residuos"]])


# ---------------------------------------------------------------------------
# 2. y 3. Estación total vs GPS y ajuste
# ---------------------------------------------------------------------------

def buscar(dic, nombre, cual, archivo):
    if nombre not in dic:
        raise SystemExit(f"No encuentro el punto '{nombre}' ({cual}) en {archivo}. "
                         "Si tiene otro nombre en la ET usar ET=GPS, por ejemplo --cierre T45=PF9")
    return dic[nombre]


def analizar_y_ajustar(R, gps, control, et_lista, args):
    et = {}
    duplicados = set()
    for p in et_lista:
        if p["nombre"] in et:
            duplicados.add(p["nombre"])
        else:
            et[p["nombre"]] = p

    ini_et, ini_gps = par_nombres(args.inicio)
    cie_et, cie_gps = par_nombres(args.cierre)
    A_et = buscar(et, ini_et, "inicio", "la ET")
    A_g = buscar(gps, ini_gps, "inicio", "el GPS")
    C_et = buscar(et, cie_et, "cierre", "la ET")
    C_g = buscar(gps, cie_gps, "cierre", "el GPS")

    R.titulo("2. ESTACIÓN TOTAL vs. GPS")
    if duplicados:
        R(f"Aviso: nombres repetidos en la ET (se usa la primera aparición para el cierre): "
          f"{', '.join(sorted(duplicados))}")

    dA = (A_g["E"] - A_et["E"], A_g["N"] - A_et["N"],
          (A_g["H"] - A_et["H"]) if None not in (A_g["H"], A_et["H"]) else 0.0)
    if math.hypot(dA[0], dA[1]) > 0.001 or abs(dA[2]) > 0.001:
        R(f"Aviso: el punto de inicio en la ET difiere del GPS: dE {dA[0]:+.3f} dN {dA[1]:+.3f} "
          f"dH {dA[2]:+.3f}. Se tiene en cuenta en el ajuste.")

    _, _, _, _, kc = factores((A_g["E"] + C_g["E"]) / 2, (A_g["N"] + C_g["N"]) / 2,
                              ((A_g["H"] or 0) + (C_g["H"] or 0)) / 2, args.faja, args.ondulacion)

    D = dist(A_g, C_g)
    az = acimut(A_g, C_g)
    uE, uN = math.sin(az), math.cos(az)        # dirección A->C
    tE, tN = uN, -uE                           # perpendicular (derecha)
    wE, wN = C_et["E"] - C_g["E"], C_et["N"] - C_g["N"]
    wH = (C_et["H"] - C_g["H"]) if None not in (C_et["H"], C_g["H"]) else None
    # el error de cierre propio de la ET excluye la diferencia que ya había en el inicio
    wE0, wN0 = wE + dA[0], wN + dA[1]
    wL = wE0 * uE + wN0 * uN
    wT = wE0 * tE + wN0 * tN
    w = math.hypot(wE0, wN0)

    R.sub("2.1 Error de cierre en %s (ET - GPS)" % cie_gps)
    R(f"Inicio {ini_gps} -> cierre {cie_gps}: distancia {D:.3f} m, acimut {dms(az)}")
    R(f"dE {wE0:+.3f}   dN {wN0:+.3f}   lineal {w:.3f} m" + (f"   dH {wH + dA[2]:+.3f}" if wH is not None else ""))
    R(f"  longitudinal (a lo largo de {ini_gps}->{cie_gps}): {wL:+.3f} m = {wL / D * 1e6:+.0f} ppm "
      f"({'la ET llega largo' if wL > 0 else 'la ET llega corto'})")
    R(f"  transversal: {wT:+.3f} m ({'a la derecha' if wT > 0 else 'a la izquierda'}) "
      f"= giro de {math.atan2(wT, D) * SEG:+.1f}\" visto desde {ini_gps}")

    est, L, cum = None, None, None
    if args.poligonal:
        est = [s.strip() for s in args.poligonal.split(",") if s.strip()]
        faltan = [s for s in est if s not in et]
        if faltan:
            raise SystemExit(f"Estaciones de --poligonal que no están en la ET: {faltan}")
        if est[0] != ini_et or est[-1] != cie_et:
            raise SystemExit("--poligonal debe empezar en el punto de inicio y terminar en el de cierre "
                             f"(nombres de la ET: {ini_et} ... {cie_et})")
        cum = [0.0]
        for i in range(1, len(est)):
            cum.append(cum[-1] + dist(et[est[i - 1]], et[est[i]]))
        L = cum[-1]
        rel = f"{L / w:,.0f}".replace(",", ".") if w > 0 else "-"
        R(f"Poligonal: {len(est) - 1} tramos, longitud {L:.3f} m -> precisión relativa 1:{rel}")

    R.sub("2.2 ¿Cuánto explica el factor de escala?")
    eL = D * (1 / kc - 1)
    R(f"Factor combinado en la zona: {kc:.8f} ({ppm(kc):+.1f} ppm)")
    R(f"Si la ET trabajó con distancias de terreno (factor de escala = 1), llega largo")
    R(f"  {eL:+.3f} m en {D:.0f} m. Error longitudinal observado {wL:+.3f} m -> remanente {wL - eL:+.3f} m.")
    R(f"Si la ET ya tenía cargado el factor, el remanente es todo el observado ({wL:+.3f} m).")
    R("(Otra causa típica de escala: la corrección atmosférica (ppm) de la ET mal cargada,")
    R(" 10 °C de diferencia equivalen a unos 10 ppm.)")

    B_g = B_et = None
    sigma_az = None
    if args.orientacion:
        ori_et, ori_gps = par_nombres(args.orientacion)
        B_g = gps.get(ori_gps)
        B_et = et.get(ori_et)
        R.sub("2.3 Orientación inicial (%s -> %s)" % (ini_gps, ori_gps))
        if B_g is None:
            R(f"El punto de orientación {ori_gps} no está en el archivo GPS.")
        else:
            dAB = dist(A_g, B_g)
            sigma_az = math.sqrt(2) * args.sigma_gps / dAB
            R(f"Distancia GPS {ini_gps}-{ori_gps}: {dAB:.3f} m, acimut {dms(acimut(A_g, B_g))}")
            R(f"Con {args.sigma_gps * 100:.1f} cm de error por punto GPS, la orientación tiene una "
              f"incertidumbre de ±{sigma_az * SEG:.0f}\",")
            R(f"  que en {D:.0f} m de distancia al cierre son ±{sigma_az * D:.3f} m transversales.")
            R(f"Giro necesario para explicar el error transversal: {math.atan2(wT, D) * SEG:+.1f}\"")
            if B_et is not None:
                dABet = dist(A_et, B_et)
                R(f"Medido con la ET: D {dABet:.3f} m (dif {dABet - dAB:+.3f} m = "
                  f"{(dABet - dAB) / dAB * 1e6:+.0f} ppm; por escala se esperaría "
                  f"{dAB * (1 / kc - 1):+.3f} m), acimut dif {ang_dif(acimut(A_et, B_et), acimut(A_g, B_g)) * SEG:+.1f}\"")
            if dAB < 0.25 * D:
                R(f"=> La base de orientación es corta ({dAB:.0f} m) frente al recorrido ({D:.0f} m): "
                  "un error chico de GPS")
                R("   en la orientación se transforma en un desplazamiento transversal grande al final.")

    if est:
        R.sub("2.4 Error esperable de la poligonal")
        sb = args.sigma_ang / SEG
        var = 2 * args.sigma_gps ** 2
        if sigma_az is not None:
            var += (sigma_az * D) ** 2
        for i in range(len(est) - 1):
            var += (sb * dist(et[est[i]], C_et)) ** 2
            leg = cum[i + 1] - cum[i]
            var += (args.sigma_dist_mm / 1000 + args.sigma_dist_ppm * 1e-6 * leg) ** 2
        sig = math.sqrt(var)
        tol = 2.5 * sig
        R(f"Supuestos: {args.sigma_ang:.0f}\" por estación (lectura+centrado), "
          f"{args.sigma_dist_mm:.0f} mm + {args.sigma_dist_ppm:.0f} ppm por distancia, "
          f"{args.sigma_gps * 100:.1f} cm por punto GPS" + ("" if sigma_az is not None else
                                                          "; sin dato de orientación"))
        R(f"Error de cierre esperable: ±{sig:.3f} m (1 sigma), tolerancia (2,5 sigma) {tol:.3f} m")
        w_sin_esc = math.hypot(wL - eL, wT)
        R(f"Cierre observado {w:.3f} m; descontando el efecto de escala {w_sin_esc:.3f} m")
        if w <= tol:
            R("=> El cierre está DENTRO de lo esperable.")
        elif w_sin_esc <= tol:
            R("=> El cierre se explica por el factor de escala no aplicado; el resto está dentro de lo esperable.")
        else:
            R("=> El cierre es MAYOR a lo esperable: revisar (punto GPS de cierre, altura de prisma/")
            R("   instrumento, constante de prisma, estación mal identificada, error de lectura).")

    # ------------------------------------------------------------------
    R.titulo("3. AJUSTE DE LA ESTACIÓN TOTAL A LOS PUNTOS GPS")

    # fracción de avance de cada punto (0 en el inicio, 1 en el cierre)
    frac_est = {}
    if est:
        for s, c in zip(est, cum):
            frac_est[s] = c / L
    vAC = (C_et["E"] - A_et["E"], C_et["N"] - A_et["N"])
    dAC2 = vAC[0] ** 2 + vAC[1] ** 2

    def fraccion(p):
        if p["nombre"] in frac_est:
            return frac_est[p["nombre"]], "poligonal"
        if p.get("estacion") in frac_est:
            return frac_est[p["estacion"]], "estacion"
        t = ((p["E"] - A_et["E"]) * vAC[0] + (p["N"] - A_et["N"]) * vAC[1]) / dAC2
        return min(1.0, max(0.0, t)), "proyeccion"

    dC = (C_g["E"] - C_et["E"], C_g["N"] - C_et["N"],
          (C_g["H"] - C_et["H"]) if None not in (C_g["H"], C_et["H"]) else 0.0)

    pares = [(A_et, A_g), (C_et, C_g)]
    usados = {A_et["nombre"], C_et["nombre"]}
    if args.usar_todos:
        for n, p in et.items():
            if n in gps and n not in usados:
                pares.append((p, gps[n]))
                usados.add(n)
    T = helmert_ajustar(pares)

    metodos = [args.metodo] if args.metodo != "ambos" else ["helmert", "bowditch"]
    if "bowditch" in metodos and not est:
        R("Bowditch requiere --poligonal (orden de estaciones). Se usa solo Helmert.")
        metodos = ["helmert"]

    R.sub("3.1 Parámetros")
    R(f"Helmert (semejanza 2D, {len(pares)} puntos: {', '.join(o['nombre'] for o, _ in pares)})")
    R(f"  escala {T['escala']:.8f} ({ppm(T['escala']):+.1f} ppm; factor combinado teórico "
      f"{ppm(kc):+.1f} ppm)")
    R(f"  giro (corrección de acimut) {-T['giro'] * SEG:+.1f}\"")
    if T["s0"] is not None:
        R(f"  s0 = {T['s0']:.3f} m; residuos:")
        R.tabla(["Punto", "vE", "vN"], [[n, fs(a), fs(b)] for n, a, b in T["residuos"]])
    else:
        R("  Con 2 puntos el ajuste es exacto: no hay redundancia. La calidad se juzga por la")
        R("  coherencia de escala y giro con lo esperable y por los puntos de verificación (3.2).")
    dif_esc = ppm(T["escala"]) - ppm(kc)
    sig_esc = math.sqrt(2 * args.sigma_gps ** 2 + (args.sigma_dist_mm / 1000) ** 2 * (len(est) - 1 if est else 5)) / D * 1e6
    tol_esc = max(10.0, 2.5 * sig_esc)
    R(f"  tolerancia de escala por precisión GPS/ET en {D:.0f} m: ±{tol_esc:.0f} ppm")
    if abs(dif_esc) <= tol_esc:
        R("=> La escala coincide con el factor combinado: la ET midió distancias de terreno y el")
        R("   ajuste las pasa a cuadrícula GK. Es la situación esperada.")
    elif abs(ppm(T["escala"])) <= tol_esc:
        R("=> Escala ~1: la ET ya aplicaba un factor (o la diferencia se compensa con otra causa).")
    else:
        R(f"=> La escala difiere {dif_esc:+.0f} ppm del factor teórico: revisar ppm atmosférico,")
        R("   constante de prisma o la coordenada GPS del cierre.")
    if "bowditch" in metodos:
        R(f"Bowditch: corrección en el cierre dE {dC[0]:+.3f} dN {dC[1]:+.3f}, repartida según la")
        R(f"  distancia acumulada sobre {L:.1f} m de poligonal.")
    R(f"Cotas: corrección {dC[2]:+.3f} m en el cierre, repartida linealmente con el avance.")

    resultados = {}
    for m in metodos:
        out = []
        for p in et_lista:
            f, origen = fraccion(p)
            if m == "helmert":
                E, N = helmert_aplicar(T, p["E"], p["N"])
            else:
                E = p["E"] + dA[0] + (dC[0] - dA[0]) * f
                N = p["N"] + dA[1] + (dC[1] - dA[1]) * f
            H = (p["H"] + dA[2] + (dC[2] - dA[2]) * f) if p["H"] is not None else None
            out.append({"nombre": p["nombre"], "codigo": p.get("codigo") or p.get("estacion") or "",
                        "E0": p["E"], "N0": p["N"], "H0": p["H"], "E": E, "N": N, "H": H,
                        "f": f, "origen": origen})
        resultados[m] = out

    verif = [n for n in et if n not in usados and (n in gps or (control and n in control))]
    R.sub("3.2 Puntos de verificación (medidos con ET y con GPS/partida, no usados en el ajuste)")
    if not verif:
        R("No hay. Recomendación: si la ET midió algún otro punto GPS o de partida, nombrarlo igual")
        R("en ambos archivos para tener un control independiente del ajuste.")
    else:
        filas = []
        for n in verif:
            ref = gps.get(n) or control[n]
            fuente = "GPS" if n in gps else "partida"
            p0 = et[n]
            fila = [n, fuente, f3(math.hypot(p0["E"] - ref["E"], p0["N"] - ref["N"]))]
            for m in metodos:
                q = next(r for r in resultados[m] if r["nombre"] == n)
                fila += [fs(q["E"] - ref["E"]), fs(q["N"] - ref["N"]),
                         fs((q["H"] - ref["H"]) if None not in (q["H"], ref["H"]) else None)]
            filas.append(fila)
        cab = ["Punto", "Ref", "Antes"]
        for m in metodos:
            cab += [f"dE {m[:4]}", f"dN {m[:4]}", f"dH {m[:4]}"]
        R.tabla(cab, filas)
        R("(d = ajustado - referencia)")

    if len(metodos) == 2:
        difs = [math.hypot(a["E"] - b["E"], a["N"] - b["N"])
                for a, b in zip(resultados["helmert"], resultados["bowditch"])]
        R("")
        R(f"Diferencia entre Helmert y Bowditch: máx {max(difs):.3f} m, media {media(difs):.3f} m")

    origenes = {}
    for r in resultados[metodos[0]]:
        origenes[r["origen"]] = origenes.get(r["origen"], 0) + 1
    if origenes.get("proyeccion"):
        R("")
        R(f"Nota: {origenes['proyeccion']} punto(s) sin estación asignada recibieron la corrección de cota"
          + (" (y la de Bowditch)" if "bowditch" in metodos else "")
          + " según su proyección sobre la recta inicio-cierre.")

    R.sub("3.3 Correcciones aplicadas")
    m0 = metodos[0]
    corr = [math.hypot(r["E"] - r["E0"], r["N"] - r["N0"]) for r in resultados[m0]]
    R(f"{m0}: {len(corr)} puntos, corrección planimétrica media {media(corr):.3f} m, máxima {max(corr):.3f} m")
    return resultados


def escribir_csv(ruta, filas, excel):
    sep = ";" if excel else ","
    num = (lambda v: "" if v is None else f"{v:.4f}".replace(".", ",")) if excel else \
          (lambda v: "" if v is None else f"{v:.4f}")
    with open(ruta, "w", encoding="utf-8-sig" if excel else "utf-8", newline="") as fh:
        fh.write(sep.join(["Punto", "Codigo", "Este", "Norte", "H",
                           "Este_ET", "Norte_ET", "H_ET", "corr_E", "corr_N", "corr_H"]) + "\n")
        for r in filas:
            fh.write(sep.join([r["nombre"], r["codigo"], num(r["E"]), num(r["N"]), num(r["H"]),
                               num(r["E0"]), num(r["N0"]), num(r["H0"]),
                               num(r["E"] - r["E0"]), num(r["N"] - r["N0"]),
                               num((r["H"] - r["H0"]) if r["H"] is not None else None)]) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Control de puntos GPS y ajuste de estación total a GPS (POSGAR 07 / GK).",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--gps", required=True, help="CSV de puntos GPS propios (Punto,E,N,H[,prec,solución])")
    ap.add_argument("--control", help="CSV de coordenadas de partida (comitente) para comparar")
    ap.add_argument("--et", help="CSV de coordenadas calculadas con la ET (Punto,E,N,H[,Estacion,Codigo])")
    ap.add_argument("--inicio", help="Punto de arranque (nombre, o ET=GPS si difieren)")
    ap.add_argument("--orientacion", help="Punto GPS al que se orientó en el arranque")
    ap.add_argument("--cierre", help="Punto de llegada medido con ET y con GPS")
    ap.add_argument("--poligonal", help="Estaciones en orden, separadas por coma, de inicio a cierre")
    ap.add_argument("--metodo", choices=["helmert", "bowditch", "ambos"], default="ambos")
    ap.add_argument("--usar-todos", action="store_true",
                    help="Usar en Helmert todos los puntos comunes ET-GPS (no solo inicio y cierre)")
    ap.add_argument("--faja", type=int, default=2)
    ap.add_argument("--formato", default="PENZ", help="Orden de columnas si no hay cabecera (PENZ, PNEZ)")
    ap.add_argument("--ondulacion", type=float, default=0.0,
                    help="Ondulación del geoide si las alturas son ortométricas (m)")
    ap.add_argument("--tol-h", type=float, default=0.03, help="Tolerancia planimétrica GPS (m)")
    ap.add_argument("--tol-v", type=float, default=0.05, help="Tolerancia altimétrica GPS (m)")
    ap.add_argument("--sigma-gps", type=float, default=0.015, help="Error planimétrico por punto GPS (m)")
    ap.add_argument("--sigma-ang", type=float, default=5.0, help="Error angular por estación (\")")
    ap.add_argument("--sigma-dist-mm", type=float, default=3.0)
    ap.add_argument("--sigma-dist-ppm", type=float, default=3.0)
    ap.add_argument("--salida", default="resultados_ajuste", help="Carpeta de salida")
    ap.add_argument("--excel", action="store_true", help="CSV de salida con ';' y coma decimal")
    args = ap.parse_args(argv)

    avisos = []
    gps_lista = leer_puntos(args.gps, args.faja, args.formato, avisos)
    gps, reps = promediar_repetidos(gps_lista)
    control = None
    if args.control:
        control, _ = promediar_repetidos(leer_puntos(args.control, args.faja, args.formato, avisos))
    et_lista = leer_puntos(args.et, args.faja, args.formato, avisos) if args.et else None

    R = Reporte()
    R("INFORME DE CONTROL GPS Y AJUSTE DE ESTACIÓN TOTAL - POSGAR 07 / Gauss-Krüger faja %d" % args.faja)
    R(f"GPS: {args.gps} ({len(gps)} puntos)"
      + (f" | Partida: {args.control} ({len(control)})" if control else "")
      + (f" | ET: {args.et} ({len(et_lista)})" if et_lista else ""))
    for a in avisos:
        R("Aviso: " + a)

    analizar_gps(R, gps, reps, control, args)

    os.makedirs(args.salida, exist_ok=True)
    if et_lista:
        if not (args.inicio and args.cierre):
            raise SystemExit("Para ajustar la ET hay que indicar --inicio y --cierre")
        res = analizar_y_ajustar(R, gps, control, et_lista, args)
        for m, filas in res.items():
            ruta = os.path.join(args.salida, f"et_ajustada_{m}.csv")
            escribir_csv(ruta, filas, args.excel)
            R(f"Archivo: {ruta}")
    ruta = os.path.join(args.salida, "informe.txt")
    R.guardar(ruta)
    print(f"\nInforme guardado en {ruta}")


if __name__ == "__main__":
    main()
