# Web para jugadores

La app de C++ es la herramienta del máster. La **web** es la vista de los jugadores: cada uno abre **su enlace personal** y
ve su ficha completa, el estado de su party y las reglas (hechizos, aptitudes, equipo…). Se actualiza sola cada pocos segundos:
lo que el máster cambia en la app (PV, condiciones, equipo) aparece en el teléfono del jugador.

```
app del máster (C++)  ──escribe──►  characters/  parties/  packs/  settings.json     (carpeta del usuario)
                                            │ lee y escribe
                                            ▼
                     skaldbok_web  (C++, sin ventana)  ──JSON──►  web/client (Vue 3, ya compilado)  ──► teléfonos
```

* **`skaldbok_web`** es un ejecutable aparte, hecho con la misma librería que la app (`gm_core`): carga los mismos packs, usa las
  mismas reglas y lee los mismos archivos. No tiene base de datos propia ni lógica duplicada. Puede correr aunque la app del
  máster esté cerrada (los jugadores ven la última ficha guardada), incluso en otra máquina con los archivos sincronizados.
* **`web/client`** es una página Vue 3 (Vite). Node solo hace falta para *compilarla*; el servidor sirve los archivos ya
  compilados (`web/client/dist`, o la carpeta `web/` junto al ejecutable en el paquete).
* Es de **lectura y de escritura acotada**: el jugador solo puede cambiar **su propia** hoja (`PATCH /api/me`) y escribir en **su** chat (`POST /api/chat`); nada más.

## Lo normal: abrir la app y listo

**`Skaldbok.bat`** abre la app del máster, y **la app arranca sola el servidor web** (**Settings › Web**: el engranaje de arriba a la derecha) y lo
cierra al salir; incluso si la app se cierra a la fuerza, el servidor se apaga solo. En **Settings › Web** ves si está funcionando,
la dirección para los jugadores (la de tu red, por ejemplo `http://192.168.1.20:8080`) y el enlace personal de cada personaje con
*Copy* y *Send in a message*. El enlace de un jugador es esa dirección más su token: `http://192.168.1.20:8080/?t=<token>`.

* Los jugadores tienen que estar en la **misma red Wi-Fi** (o usar un túnel, ver más abajo).
* La primera vez, Windows pregunta si permitir `skaldbok_web` en la red: **permitir en redes privadas**.
* Si el puerto 8080 lo usa otro programa (Jellyfin usa 8096, otros usan 8080), cámbialo en Settings › Web › *Port* › *Apply*.
* En Settings › Web se puede desactivar el arranque automático; también se puede apagar todo el módulo en Settings › General › Modules.
* La página necesita compilarse una vez (`cd web && npm install && npm run build`); `Skaldbok.bat` lo hace solo si falta y hay Node.

## Ponerlo en marcha a mano (sin la app, por ejemplo en otro equipo)

```
# 1) compilar la página (una vez, con Node 20+)
cd web && npm install && npm run build

# 2) compilar el servidor junto con el resto del proyecto
powershell -ExecutionPolicy Bypass -File scripts\build.ps1        # crea build\release\skaldbok_web.exe

# 3) arrancarlo
build\release\skaldbok_web.exe --public-url http://mi-direccion
```

Al arrancar crea el archivo `web-access.json` (en la carpeta del usuario) con un **token secreto por personaje**. Los enlaces:

```
skaldbok_web --links                    # imprime el enlace de cada jugador
skaldbok_web --regenerate Brenna        # enlace nuevo para ese personaje: el viejo deja de funcionar al instante
```

También aparecen en la app del máster, en la ficha de cada personaje (**Player's web page**), con botón *Copy link* y *Send it
in a message*. Un personaje nuevo recibe su enlace en cuanto el servidor lo ve.

### Opciones

Variables de entorno o argumentos (`--port`, `--host`, `--public-url`, `--prefs`, `--data`, `--static`):

| | por defecto |
|---|---|
| `PORT` | `8080` |
| `HOST` | `0.0.0.0` (todas las interfaces; usa `127.0.0.1` para solo esta máquina) |
| `PUBLIC_URL` | `http://localhost:<puerto>`: la dirección que escriben los jugadores; va dentro de los enlaces |
| `SKALDBOK_PREFS` | la carpeta del usuario de la app (`%APPDATA%\skaldbok\gm`) |
| `SKALDBOK_DATA` | la carpeta `data/` con `skaldbok.db` y `packs/core` (se busca junto al ejecutable) |
| `STATIC_DIR` | la página compilada (`web/` junto al ejecutable, o `web/client/dist`) |
| `MAX_FAILURES`, `FAILURE_WINDOW_MS` | 20 intentos fallidos por dirección cada 10 minutos, luego se bloquea |
| `TRUST_PROXY` | `1` si está detrás de un túnel/proxy: toma la dirección real de `X-Forwarded-For` |

## Que los jugadores lleguen desde fuera de tu casa

En la misma Wi-Fi no hace falta nada: el módulo Web ya da enlaces con la dirección de tu PC. Desde otra red hay que exponerlo, y
conviene **HTTPS** (el enlace lleva el token; sobre HTTP plano viajaría a la vista). El servidor habla HTTP simple a propósito;
hay tres caminos, del más fácil al más trabajoso:

**A. Cloudflare Tunnel rápido (sin cuenta, sin abrir puertos, HTTPS incluido).** Con la app abierta:

```
winget install Cloudflare.cloudflared          # una vez
cloudflared tunnel --url http://localhost:8080
```

Escribe una dirección `https://algo-al-azar.trycloudflare.com`. Cópiala en **Settings › Web › Address players use › Apply**: los
enlaces de los jugadores pasan a usar esa dirección (y el servidor cuenta los intentos fallidos por cliente real, no por el túnel).
Los jugadores los abren desde cualquier lugar, sin instalar nada. Limitaciones: la dirección **cambia cada vez** que reinicias
`cloudflared` (hay que volver a copiar los enlaces) y el túnel solo existe mientras esa ventana esté abierta. Para una dirección
fija hace falta una cuenta gratuita de Cloudflare y un dominio propio (túnel con nombre).

**B. Tailscale (red privada, sin HTTPS que configurar, sin exponer nada a Internet).** Tú y cada jugador instalan Tailscale
(gratis) y tú los invitas a tu red (o compartes tu PC con ellos). Después cada jugador abre
`http://<nombre-o-ip-100.x.x.x-de-tu-pc>:8080/?t=<token>`: el tráfico va cifrado por Tailscale. Pon esa dirección en
**Address players use**. Es lo más privado, pero cada jugador tiene que instalar Tailscale.

**C. Abrir el puerto en el router** (reenvío del 8080 + una dirección fija o DDNS) y un proxy inverso (Caddy, nginx) con
certificado. Funciona, pero expone tu casa a Internet y hay que mantenerlo: solo si sabes lo que haces.

Una dirección que empieza por `https://` en **Address players use** hace que la app arranque el servidor con `--trust-proxy`
(usa `X-Forwarded-For` como dirección del cliente). Si lo arrancas a mano detrás de un proxy, agrégalo tú
(`skaldbok_web --trust-proxy`, o `TRUST_PROXY=1`). No lo actives si el servidor está expuesto directamente, sin proxy.

## La hoja del jugador

La pestaña **My character** sigue la primera hoja (la verde) de la hoja de personaje oficial y está pensada para el teléfono: nombre en el
pergamino, las seis gemas de atributos con su condición (el rombo se enciende en rojo), PV y PW como círculos que se vacían, bonos y
movimiento, habilidades en pergamino (rombo = marca de avance; las no entrenadas, en gris con su probabilidad base), habilidades y
hechizos que se abren con su texto, armas, armadura y casco con su valor y su *Bane on*, inventario numerado, monedas, recuerdo y objetos
diminutos. En pantallas anchas usa las tres columnas de la hoja impresa; en el teléfono, un solo carril en el orden en que se usa en la mesa.

## Chat con el máster

`GET /api/chat` devuelve la conversación del jugador (más vieja primero: `id`, `at`, `from` = `gm` o `player`, `kind` = `message` o `broadcast`, `to`, `text`,
`image`) y dónde leyó cada lado (`gmRead`, `playerRead`). `POST /api/chat` `{text?, image?}` escribe al máster (texto de hasta 4000 letras; `image` en base64:
PNG, JPEG, GIF o WebP de hasta 8 MB, reconocido **por sus bytes**, no por un nombre: un SVG, que puede llevar código, se rechaza). `POST /api/chat/read`
`{upTo}` marca lo leído. `GET /api/chat/<id>/image` sirve una imagen **solo de la conversación del propio jugador**: el archivo sale de la conversación, nunca de la
petición, y otro jugador (o una ruta con `..`) recibe 404. La página baja la imagen con el token en la cabecera y la muestra desde una dirección `blob:` (la CSP
permite `img-src blob:`). Un jugador nunca escribe a otro jugador: su única conversación es con el máster.

## Editar la hoja

`GET /api/me` trae, además de lo derivado, el `doc` (los campos editables tal cual están en el archivo), `issues` (lo que rompe las reglas y nadie aprobó, cada uno con
`key`, `message` y `status` = `pending` o `rejected`), `locked` y `revision`. `PATCH /api/me` `{set: {...}}` cambia campos: los valores van enteros, salvo
`attributes` y `coins`, que se mezclan clave por clave. Se valida el tipo y el tamaño de todo (400 con el motivo si algo está mal, sin aplicar nada) y solo se aceptan los campos
de la hoja: `kin`, `profession`, `school`, `id`, `revision`, `locked` y `reviews` son del máster (400). Una hoja bloqueada devuelve 423. **Romper una regla no se rechaza**: se guarda y
queda marcado en `issues`. La respuesta es la hoja nueva. Cada cambio queda en `changes/<personaje>.json` (quién, qué y el valor anterior) y sube el `revision`.

Como la app del master y el servidor escriben los mismos archivos, ninguno reescribe la hoja entera: cada escritura toma el archivo tal como está y le aplica
solo lo que cambió, de forma atómica; la app compara el **contenido** de los archivos (no sus fechas, que solo tienen la precisión del reloj del sistema, ~15 ms).

## Qué ve un jugador, y qué no

Cada respuesta se arma campo por campo en `src/web/web_views.cpp`; nada se pasa "tal cual", así que un campo nuevo en los
archivos del máster **sigue siendo privado** hasta que se añade a propósito.

| De sí mismo | De su party |
|---|---|
| Su ficha completa: atributos, PV/PW, condiciones, habilidades, aptitudes y hechizos con su texto, equipo con sus estadísticas, monedas, debilidad, recuerdo, apariencia y las **notas de la ficha** | El nombre de la party y de cada miembro: raza, profesión, edad, PV, PW y condiciones |

Además, las **reglas**: hechizos, aptitudes, habilidades, razas, profesiones y equipo del Core y de los packs de homebrew
**activos**.

**Nunca**: criaturas y tablas (son del máster), las notas de la party, las conversaciones de otros jugadores, las fichas de
otros jugadores (solo su resumen de party), ni los tokens. Un pack que el máster apaga en Settings deja de verse en la web.

Seguridad:

* El token son 128 bits aleatorios; identifica **un** personaje y el personaje lo elige el servidor por el token, nunca un parámetro.
* El token va en la cabecera `Authorization: Bearer` (nunca en la URL de la API, para que no quede en registros). La página lo
  guarda en el navegador y lo **borra de la barra de direcciones** al abrir el enlace.
* Tras 20 intentos fallidos desde una dirección se bloquea unos minutos. Los mensajes de error no dicen por qué falló.
* GET, y solo dos escrituras (`PATCH /api/me`, `POST /api/chat` y su marca de leído); el token va en una cabecera, no en una cookie, así que no hay CSRF; hasta 60 escrituras cada 10 s por personaje; cuerpos de más de 64 KB se rechazan (salvo una imagen del chat, hasta 12 MB en base64); respuestas de la API con `Cache-Control: no-store`; cabeceras `nosniff`, `X-Frame-Options: DENY`,
  `Referrer-Policy: no-referrer` y una CSP estricta; los textos se muestran siempre como texto (nunca como HTML).
* Los archivos del máster no se escriben de forma atómica: si una petición cae justo en medio de un guardado, el servidor sigue
  mostrando la última versión buena, y **nunca** borra el token de un jugador por no poder leer su archivo un instante.

## API (para quien quiera hacer otro cliente)

Todas las rutas menos `/api/health` piden `Authorization: Bearer <token>`.

| | |
|---|---|
| `GET /api/health` | `{"ok":true}` |
| `GET /api/me` | la ficha del jugador, con los números derivados (movimiento, bonificación de daño, carga, nivel de cada habilidad) |
| `GET /api/party` | `{"party": {"name", "members":[…]}}` o `{"party": null}` |
| `GET /api/content` | los tipos de regla (con su cantidad) y los packs cargados |
| `GET /api/content/<tipo>?q=texto` | `spells`, `abilities`, `skills`, `kin`, `professions`, `weapons`, `armor`, `gear` |

Errores: `401` enlace no válido, `429` demasiados intentos, `404` ruta o tipo desconocido, `405` método no permitido.

## Desarrollo de la página

```
cd web
npm run dev          # http://localhost:5173, reenvía /api a http://localhost:8080 (API_URL para otro)
npm test             # 19 pruebas (Vitest): enlace y token, la ficha, el refresco, la desconexión
npm run build        # web/client/dist
```

Con `skaldbok_web` corriendo en el puerto 8080, `npm run dev` recarga la página al vuelo. Las pruebas del cliente usan
respuestas de ejemplo (`web/client/src/test/fixtures`) hechas con datos inventados, no con texto de los libros.

## Pruebas del servidor

`tests/web_test.cpp` (parte de `ctest`): autenticación, la ficha y sus números, qué se filtra y qué no, la party, las reglas y
los packs apagados, cambios en vivo, archivos a medio escribir, tokens (persistencia, regeneración, personajes nuevos y
borrados), bloqueo por intentos, la página estática y HTTP real (sockets). Usa archivos escritos por la propia app del máster
(`tests/fixtures/web`) y un Core inventado, así que corre sin los libros; con `data/` presente además comprueba las claves
reales del Core.
