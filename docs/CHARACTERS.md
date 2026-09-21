# Datos de personajes

Cada personaje es **un archivo JSON** en la carpeta de personajes del usuario (`Settings › Where things are ›
Characters`; en Windows `%APPDATA%\skaldbok\gm\characters\`). El nombre del archivo (sin `.json`) es el `id` del
personaje. La app es hoy una herramienta del máster; estos archivos están pensados para que una futura web app de los
jugadores los lea (y escriba) tal cual.

* **Exportar / importar** un personaje: botones *Export…* y *Import…* del módulo Characters. Importar **siempre crea
  un personaje nuevo** (nuevo `id`), nunca pisa uno existente.
* La app escribe el archivo entero cada vez que algo cambia en la ficha.
* Lo que un personaje toma del contenido del juego (raza, profesión, habilidades, aptitudes, hechizos, equipo) se guarda como
  **referencia**: la clave estable de la entrada (`core/kin/human`, `frostmarch/spell/rime-ward`) **y** su nombre en el
  momento de guardar. Con la clave se encuentra la entrada completa; con el nombre la ficha se lee aunque el pack ya no esté.

## Formato (`format: 1`)

```json
{
  "format": 1,
  "id": "c-19a2b3c4d5e-07f",
  "name": "Brenna", "nickname": "Grimjaw", "player": "Sebastián",
  "age": "adult",                                   // young | adult | old
  "kin":        { "key": "core/kin/human",           "name": "Human" },
  "profession": { "key": "core/profession/fighter",  "name": "Fighter" },
  "school": "",                                     // escuela de magia de un mago
  "attributes": { "STR": 15, "CON": 13, "AGL": 12, "INT": 9, "WIL": 10, "CHA": 11 },
  "hp": 10, "wp": 10,                               // valores ACTUALES
  "hp_bonus": 0, "wp_bonus": 0,                     // p. ej. las aptitudes Robust / Focused
  "conditions": ["Scared"],                         // Exhausted Angry Sickly Scared Dazed Disheartened
  "skills": [
    { "key": "core/skill/axes", "name": "Axes", "attribute": "STR", "level": 12, "trained": true, "marked": false }
  ],
  "abilities": [ { "key": "core/ability/adaptive", "name": "Adaptive" } ],
  "spells":    [ { "key": "core/spell/fetch", "name": "Fetch" } ],
  "weapons":   [ { "name": "Morningstar", "key": "core/weapon/morningstar" } ],   // a mano; los escudos cuentan como armas
  "armor":     { "name": "Chainmail", "key": "core/armor/chainmail" },           // o null
  "helmet":    null,
  "inventory": [ { "name": "Torch", "key": "core/gear/torch" }, { "name": "Food rations", "count": 4 } ],
  "tiny_items": ["a bone whistle"],
  "coins": { "gold": 0, "silver": 3, "copper": 0 },
  "weakness": "…", "memento": "…", "appearance": "…", "notes": "…",
  "revision": 7,                       // +1 en cada guardado, lo haga la app o el servidor web
  "locked": true,                      // opcional: el máster bloqueó la edición del jugador
  "reviews": { "encumbrance": { "status": "approved", "value": "11/8", "at": "…" } },   // fallos de reglas que el máster juzgó
  "created_at": "2026-09-20T14:03:09Z", "updated_at": "2026-09-20T14:10:42Z"
}
```

Reglas de lectura (la app las aplica y una web app debería hacer lo mismo):

* Un campo que falta toma su valor por defecto; un archivo de una versión más nueva con campos que no conoces se puede leer
  ignorando esos campos (pero **al reescribirlo se perderían**: conserva el objeto original si lo modificas).
* `skills` lista solo las habilidades **entrenadas**, con marca de avance, o con nivel cambiado. Una habilidad que no está
  en la lista vale su **probabilidad base**. `level: 0` significa «la probabilidad base».
* `key` puede faltar (algo escrito a mano): entonces vale `name`.

## Números derivados (no se guardan)

Del Rulebook, capítulo 2. Están implementados en `src/character.cpp` y probados en `tests/character_test.cpp`.

| dato | regla |
|---|---|
| Probabilidad base de una habilidad | según su atributo: 1–5 → 3, 6–8 → 4, 9–12 → 5, 13–15 → 6, 16–18 → 7 |
| Habilidad entrenada al crear | el doble de la probabilidad base (máximo 18) |
| PV máximos / PW máximos | CON / WIL (+ `hp_bonus` / `wp_bonus`) |
| Movimiento | el de la raza (`movement` del pack) + modificador de AGL: 1–6 → −4, 7–9 → −2, 10–12 → 0, 13–15 → +2, 16–18 → +4 |
| Bonificación de daño (FUE y AGL, por separado) | hasta 12: ninguna, 13–16: +D4, 17 o más: +D6 |
| Límite de carga | mitad de FUE redondeada hacia arriba (+2 con mochila); las raciones cuentan 1 objeto cada 4 |

## Creación

El asistente del módulo Characters sigue el orden del libro (raza, profesión, edad, atributos, habilidades, aptitud heroica
o magia, equipo, nombre y detalles) y decide en `src/creation.cpp` qué es válido:

* **Edad** — Young: AGL y CON +1, 8 habilidades entrenadas (6 de la profesión + 2). Adult: sin cambios, 10 (6+4).
  Old: FUE, AGL y CON −2, INT y WIL +1, 12 (6+6). Ningún atributo pasa de 18.
* **Atributos** — 4D6 quitando el dado más bajo, uno por atributo; al terminar se pueden intercambiar dos. También se
  pueden escribir a mano (3–18).
* **Habilidades** — 6 de la lista de la profesión (las de la escuela elegida para un mago, que debe incluir la escuela) y el
  resto libres, sin repetir. Las de categoría `magic` solo vienen por la profesión.
* **Aptitud heroica** — una de las de la profesión. Los magos no tienen: eligen 3 hechizos de rango 1 y 3 trucos de su
  escuela o de *General Magic*.
* **Equipo** — uno de los tres conjuntos de la profesión (o su tirada de D6: 1–2, 3–4, 5–6); los dados del conjunto se tiran.
* Debilidad, recuerdo y apariencia son opcionales y se pueden tirar en las tablas del pack (`role` en `tables.json`).

El botón **Random** crea un personaje completo tirando todo como indica el libro; los tests comprueban que 500 personajes
aleatorios cumplen siempre las reglas.

## Edición desde dos lugares

El jugador edita su hoja desde la web y el máster desde la app, a la vez. Por eso **nadie reescribe el archivo entero**: cada guardado toma el archivo tal como está y
aplica solo lo que cambió (un "set": los campos que difieren, enteros, salvo `attributes` y `coins` y `reviews`, que van clave por clave), con escritura atómica y `revision` + 1.
La app relee los archivos ~3 veces por segundo comparando su contenido y muestra lo que cambió el jugador.

`reviews` guarda el fallo de reglas que el máster aprobó (`approved`) o rechazó (`rejected`), con el `value` juzgado (por ejemplo `"11/8"` = 11 objetos con límite 8): una
decisión vale solo para eso, y se olvida cuando el problema se arregla. Los fallos que se marcan: PV/PW sobre su máximo, un máximo cambiado sin la aptitud (Robust, Focused),
un atributo fuera de 3–18, una habilidad sobre 18 y más carga que el límite.

## Parties

Una **party** agrupa a los personajes que juegan juntos. Es un JSON por party en
la carpeta `parties/` (junto a `characters/`); el nombre del archivo es su `id`.

```json
{
  "format": 1,
  "id": "p-1a0c0163f9b-d77",
  "name": "The Misty Vale party",
  "members": ["c-1a0c0163f99-f52", "c-1a0c0163f9a-000"],      // ids de personajes, sin repetir
  "notes": "Notas del máster: no las ve nadie más.",
  "created_at": "2026-09-20T14:03:09Z", "updated_at": "2026-09-20T14:10:42Z"
}
```

* Un personaje puede estar en más de una party; borrar un personaje lo saca de todas.
* Los jugadores ven de su party solo el nombre y el estado de los miembros (ver [`WEB.md`](WEB.md)); `notes` no.

## Acceso web

El servidor web (`skaldbok_web`) guarda un token secreto por personaje en `web-access.json` (en la carpeta del usuario, junto a
`characters/`). La app del máster solo lo **lee** para mostrar el enlace de cada jugador; no es parte del archivo del personaje.
