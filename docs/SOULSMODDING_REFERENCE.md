# Souls Modding reference: Dark Souls Remastered

Research snapshot: **24 October 2026**. Entry point: [DSR reference hub][hub].
This is a searchable reference and evidence map, **not a new execution plan**.
README, ENGINEERING, TESTING and VERIFICATION_RUNBOOK retain their authority.

## What was preserved

- **146 raw wiki pages**, including **all 75 direct internal destinations** of
  the DSR hub. This includes short index/stub pages, not 146 complete specifications.
- **6 complete linked public workbooks / 27 tabs / 13,360 serialized rows**.
  Counts include headers, introductory text, blank-valued rows and other games;
  they are not counts of unique DSR facts. The original referenced CSV tabs are
  retained separately too.
- **298 upstream source/documentation files**, pinned to commit IDs: Paramdex,
  SoulsFormatsNEXT, DSAnimStudio, Soulstruct, Soulstruct for Blender, ESDLang,
  character-state metadata, and the Smithbox README.
- Animation IDs, two event-flag references, and linked dummy-point, DDS and Havok
  guides. No linked executable, tool archive, game asset or replacement ESD was
  downloaded or executed.
- Full bodies, links, headings, row counts, retrieval metadata and SHA256 hashes
  are private local research under `build/research/soulsmodding-20261024/`.
  The durable findings and source map are this file.

**Coverage limitations:** 199 wiki destinations were requested; 53 returned HTML
instead of raw page content. They remain explicitly unqualified in the manifest,
not silently counted as documentation. Many format links are missing-page
templates. The crawl is bounded, depth <= 3, and relevance-filtered outside the
DS1/DSR/common namespaces; it is not a whole-site archive. Videos, Discord,
images/attachments and every external link have not been exhaustively reviewed.
One unrelated FXR spreadsheet export failed. Smithbox's recursive source tree
exceeded the collection size bound; its README was recovered separately.

## Evidence levels and compatibility

1. **Wiki observation:** community-authored descriptions and enum labels. Useful
   names and hypotheses, sometimes incomplete, cross-game, stale or contradictory.
2. **Upstream corroboration:** a parser, editor template or PARAMDEF contains the
   field/type/interpretation. This establishes what that tool understands, not
   independent proof of the engine's runtime implementation.
3. **Local DSR evidence:** pinned game bytes and correctly attributed native
   observations. Only this can qualify an address, ABI, ownership or live effect.

The DSR hub deliberately links DS1 pages. A wiki **PARAM row offset is not a
ChrIns, collision event, pointer-chain or vtable offset**. In particular, none
of the row offsets below identify our native event `+0xF0/+0xF4` fields, relocate
our raw stamina read, or make an unverified native damage/guard call safe.
Native acceptance still lives in [NATIVE_MELEE_CONTRACT.md](NATIVE_MELEE_CONTRACT.md)
and [ARM_IK_EVIDENCE.md](ARM_IK_EVIDENCE.md).

### DS1 versus DS1R: checked, not assumed

At [Paramdex snapshot P1][p1], the cache contains **51 DS1R PARAMDEFs and 49 DS1
PARAMDEFs**. Comparing XML `Fields/Field/@Def` strings in order gives 48 matching
filenames: **43 identical ordered field definitions, 5 different**. "Identical"
here does not compare every description/default/range or the actual game rows.

| Definition | Observed DS1R difference from paired DS1 definition |
|---|---|
| `EquipParamWeapon` | `s16 levelSyncCorrectID` and six padding bytes replace eight padding bytes. |
| `EquipParamGoods` | Adds `u8 enable_pvp:1`. Do not import the DS1 bitfield blindly. |
| `ReinforceParamWeapon` | Introduces `u8 reinforcementLevel`; DS1's nine-byte padding declaration is not retained as such. |
| `ToneCorrectBank` | Adds `f32 sfxMultiplier = 1`. |
| `ToneMapBank` | Four bloom fields change from `s8` to `s16`; adds `f32 inverseToneMapMul = 3`. |

`CoolTimeParam`, `LevelSyncParam` and `WhiteCoolTimeParam` have no same-named DS1
definition in this snapshot. AtkParam, BehaviorParam, BulletParam, SkeletonParam,
MoveParam, ThrowParam and NpcParam are byte-identical XML between the two cached
directories. CharaInitParam, LightBank, LightScatteringBank and SpEffect have the
same ordered field declarations but different XML bytes. Machine-readable detail:
`primary/paramdex-comparison.json`. **No conclusion about runtime class layouts
or blanket PTDE/DSR file interchangeability follows from this comparison.**

## 1. Combat: the highest-value links

### Behavior selection is distinct from damage and pose

The [TAE overview][tae], [DS1 attack template P2][p2-tae], [BehaviorParam page][behavior]
and [editor BehaviorParam reader][p2-behavior] describe complementary pieces:

- TAE type **1**, `AttackBehavior`, supplies `AttackType`, `AttackIndex` and
  `BehaviorJudgeID`. The template labels attack types 0 Standard, 2 ForwardR1,
  62 PlungingAttack and 64 Parry; type 1 is explicitly uncertain.
- Weapon `behaviorVariationId` or NPC `behaviorVariationId`, together with the
  event's judge ID, identifies a behavior lookup context. A judge ID alone is
  not necessarily a complete BehaviorParam row ID.
- BehaviorParam contains `variationId`, `behaviorJudgeId`, `refType`, `refId`,
  `sfxVariationId`, action stamina/MP costs, category and humanity cost.
  `refType` 0/1/2 selects Attack/Bullet/SpEffect. AtkParam_PC and AtkParam_Npc
  must not be conflated merely because they share a definition.
- TAE type **2** is `BulletBehavior`; type **5** is `CommonBehavior` in the DS1
  template. These are separate event forms, not interchangeable melee calls.

**DSVR implication (research hypothesis):** preserve the selected native attack
context and its costs/defense rules while replacing animation-derived contact
geometry with physical geometry at a verified boundary. Correcting a hand pose
or writing HP is not equivalent to that. These sources do **not** establish the
runtime lookup formula, scheduling, allocation or lifetime needed for the binding.

### AtkParam: geometry, damage, guard and side effects

[Wiki table][atk]; corroborating field declarations: [DS1R AtkParam XML][p1-atk].
Offsets here are **serialized PARAM-row-relative** and must be checked against
the selected game's definition before use.

| Row location | Field family | Why it matters |
|---|---|---|
| `0x00..0x0C` | `hit0_Radius` through `hit3_Radius`, f32 | Four sphere/capsule hit slots; not one universal weapon reach value. |
| `0x10`, `0x14` | `knockbackDist`, `hitStopTime` | Separate reaction/animation-stop properties. |
| `0x18..0x28` | `spEffectId0..4`, s32 | On-hit effects are part of an attack's semantics. |
| `0x2C..0x32` | `hit*_DmyPoly1`, s16 | First marker/end-point references. |
| `0x34..0x3A` | `hit*_DmyPoly2`, s16 | Second marker/end-point references. The upstream Japanese description explicitly says **-1 means sphere**. |
| `0x3E..0x4E` | Physical/magic/fire/lightning/stamina, repel and poise corrections | Player attacks can modify weapon-derived bases, not just use NPC-style direct values. |
| `0x50..0x62` | Base damage, stamina, poise, throw and object-attack values | Object destruction and character damage are not interchangeable. |
| `0x64`, `0x66` | `guardStaminaCutRate`, `guardRate` | Guard stability/absorption modifiers, separate from attacker action stamina cost. |
| `0x68` | `throwTypeId` | Link to paired throw/critical-attack context. |
| `0x6A..0x6D` | Per-slot hit types | Several collision classifications. |
| `0x6E..0x71` | `hti*_Priority` | Multiple simultaneous hit slots have priority handling. Preserve the upstream spelling. |
| `0x72..0x7B` | Damage level, map-hit type, guard-cut cancel, attributes, impact materials | Reaction, map collision, sound and particles need their own attribution. |
| `0x7C` | `hitSourceType` | Wiki enum: 0 weapon, 1 body, 2..33 part 0..31. Resolve the owning model, not just the numeric dummy ID. |
| `0x7D` | `throwFlag` | Throw transition versus throw itself are different cases. |
| `0x7E` bits | `disableGuard`, `disableStaminaAttack`, `disableHitSpEffect`, AI miss notification, repeated hit SFX, arrow/ghost flags, `isDisableNoDamage` | Ignoring guard, suppressing guard stamina damage and ignoring ordinary i-frames are distinct flags. |

The wiki describes `isDisableNoDamage` as bypassing ordinary i-frames but not
complete invulnerability. It is not proof that a roll or parry succeeded in a
capture. `disableStaminaAttack` is not the same field as BehaviorParam's action
cost. Do not flatten these into one "stamina" observation.

### Weapon and shield context

[EquipParamWeapon][weapon] separates:

- Behavior variation, equipment model ID, reinforcement and correction/scaling.
- Base physical/magic/fire/lightning damage and weapon-status SpEffects.
- Physical/magic/fire/lightning guard cut rates; `guardAngle`,
  `staminaGuardDef`, `guardBaseRepel`, `guardLevel` and guard-cut modifiers.
- `parryDamageLife`, `attackBaseParry`, `defenseBaseParry`, `enableGuard` and
  `enableParry`. The wiki says parry lifetime is capped by TAE; this remains
  a community interpretation requiring native timing corroboration.
- `wepmotionCategory`, `spAtkCategory`, `wepMotionOneHandId` and
  `wepMotionBothHandId`: attack set, special attack set and passive stance sets
  are not one shared number. See the [motion category list][motion].

[DS1R row-name metadata][p1-names] labels our observed equipment IDs:
**500000 Uchigatana**, **1401000 Wooden Shield**, **1408000 Leather Shield**.
These names improve trace annotation; they do not prove the loaded row contents,
the active weapon slot, native guard outcome or exact upgrade state.

### Parry, riposte, backstab and guard are separable

- [ThrowParam][throw] has attacker/defender character IDs, distance/angle/height
  conditions, linked attack/defender animations, throw type, escape-cycle
  properties, and attacker/defender attachment dummies. Its wiki enum includes
  parry type 8 and boosted parry type 12. This is stronger motivation to keep
  parry success separate from the subsequent critical animation/damage.
- [Stun/damage-level reference][stun] distinguishes c0000 reactions from other
  victims; enemy reaction animations can be shared across several damage levels.
  A stagger animation or HP delta alone is not a unique parry classifier.
- [NpcParam][npc] contains behavior variation, collision dimensions, HP,
  stamina, poise, resistances, team type and initial SpEffects. Area scaling,
  buffs and collision mode can change a comparison without changing the model ID.
- [Area scaling][area] lists effects 7001..7015 and Gravelord effect 7100, with
  explicit area exceptions. For example, the wiki assigns Burg 7002 and Parish
  7003 different HP/damage multipliers. Record the actual actor's context rather
  than expecting every hollow to share a damage baseline.

## 2. TAE, animation IDs and state machines

### TAE is gameplay timing, not just animation decoration

The [TAE page][tae] describes floating-point-second event intervals; the editor
snaps to 30-fps increments, but this does not imply the engine only updates at
30 Hz. [DSAnimStudio's README][p2] identifies attack/projectile activation,
i-frames, parry windows, SpEffects, cancellation, particles, sound, camera shake,
render masks and extra animation layers as timeline-driven behavior.

The pinned [DS1 template][p2-tae] offers more specific research handles than the
general wiki page:

| Event | Upstream template labels / research use |
|---|---|
| Type 0, `ChrActionFlag` | Flag 3 Guarding (`ArgB`: guard BehaviorJudgeID); 5 Parry Possible; 8 Invincible; 18 cancel after successful parry; 28 Disable Foot IK; 43 Enable Foot IK; 50 disable character capsule collision; 63 Parry Timing; 73 Parry Possible State; 94 Perfect Invincibility. |
| Type 1, `AttackBehavior` | Typed attack selector plus attack index and behavior judge ID. |
| Type 2 / 5 | BulletBehavior / CommonBehavior, separate payloads. |
| Type 32, `SetWeaponStyle` | 1 right weapon one-handed, 2 left weapon two-handed, 3 right weapon two-handed in this template. |
| Type 33, `SwitchWeapon` | Weapon slot selection event. |

The shared [wiki event descriptions][tae] also document type 225 stamina
regeneration, 224 rotation speed, 232 vertical upper-body aiming and 236 root
motion reduction. Treat them as **cross-game reference**, checking the DS1
template and the actual DSR TAE before using a payload or its units. In
particular, event type, nested flag ID and attack type are different numeric
namespaces: "64 Parry" is an AttackType label, not a universal TAE event number.

The template's one-/two-handed labels are consistent with the narrower local
stance 3->1 observation, but they do not prove that the event enum is the same
runtime field, nor generalize the observation to every character lifecycle.

### Attack animation ID convention

[Attack animation schema][anim-schema] gives:

`animation ID = category * 10000 + hand offset + action offset`

Hand offsets: **3000 right hand, 4000 both hands, 5000 left hand**. Common action
offsets: 000/001/002 quick-chain variants; 100 kick; 201/202/203 ripostes; 300
strong attack; 400..403 backstabs; 500 running; 600/640 jumping; 800/801/810
plunge start/loop/end; 900 evade follow-up; 980 Hornet Ring riposte. The page
documents +5 for a zero-durability variant and separate whiff/deflection offsets.

Examples from the [category list][motion]: dagger 20, straight sword 23,
greatsword 25, katana 29, spear 36, shield 48; special category 91 is labeled
the improved Buckler/Target Shield parry. **This is an ID-decoding convention,
not a formula for BehaviorParam row selection or a native animation-call ABI.**
The external [animation list][anim-list] is separately cached as HTML because
the wiki's own animation page is only a link.

### ESD: active actions and passive reactions

[ESD overview][esd] describes entry commands, ordered conditions/transitions,
while commands and exit commands. It identifies player action ESD, enemyCommon
and talk ESD as different uses. File naming varies between descriptions/tools;
inspect actual archive contents rather than inventing a `.chresd` path from a
single page's prose.

The [character-state workbook][states] has **two tabs**, not just the linked
active tab: `StateGroup1 (active)` (278 rows) and `StateGroup0 (passive)` (226).
The [wiki state list][esd-states] supplies useful community labels:

| Group | State labels worth searching |
|---|---|
| Active | 0 idle; 32..35 one-hand rolls; 46/48/49 one-hand quick attacks; 75..81 one-hand guard transitions; 86 left-hand special move, including parries **and Gold Tracer L2**; 94..97 two-hand rolls; 107/109/110 two-hand quick attacks; 121..128 two-hand guard transitions; 185..203 stance/weapon handling; 225 backstabbing. |
| Passive | 5 one-hand wall bounce; 7 two-hand wall bounce; 39 parried and ripostable; 113 getting riposted. |

State numbers are **group-local ESD identifiers**, not animation IDs, TAE flags,
ChrIns offsets or proof that the current actor is in that state. State 86 alone
cannot establish parry success. The active/passive workbook is richer than the
short wiki list and should be consulted before drawing coverage conclusions.

[Xenthalos's metadata README][p5] warns that its repository is **not merely a
neutral label pack**: it includes manually edited replacement ESD, merged/removed
conditions, known historical bugs and editor-save corruption warnings. Only
README and `.meta` were cached; **no replacement game ESD was downloaded**.
Do not contaminate our vanilla comparison with it.

[ESDLang][p6] translates ESD into a Python-like language that **cannot be run as
Python**. Its conditions wait for an eligible branch, unlike ordinary Python
if-statements; an omitted fallback may block progression. Use an editor/compiler
with explicit DS1 support and compare round trips before any controlled use.

## 3. Models, skeletons, attachment markers and IK

### FLVER markers connect visual geometry to attacks

[FLVER overview][flver] and [SoulsFormatsNEXT Dummy.cs][p3-dummy] identify:
position, forward/up vectors, signed 16-bit reference ID, parent bone index,
attachment bone index, attachment-use flag and upward-vector flag. The **parent
bone and attachment bone are not interchangeable**. Dummy indices, reference
IDs, skeleton indices and bone names are also different identifiers.

The wiki and [Soulstruct for Blender][p4] say multiple markers can share the
same reference ID. The [DSAnimStudio dummy manager][p2-dummy] stores a list per
reference ID, providing independent editor-side corroboration of multiplicity.
An importer or future fixture that uses `reference ID -> one point` loses data.
Resolve the correct source model (weapon/body/part), marker set, skeleton,
coordinate system and current pose together.

For an independent DSR geometry fixture, a useful **future experiment**, not a
completed result, is to compare weapon FLVER dummy IDs/positions against the
selected AtkParam endpoints and a native observed capsule on the same actor and
update. Do not regenerate a supposedly independent fixture from our solver.

### SkeletonParam is mostly neck/foot controls, not an arm-scaling API

[SkeletonParam][skeleton] exposes neck gain/priority/angle; ankle-ground heights;
knee-axis and twist-axis enums; toe local position; raycast up/down distances;
foot planting/raising thresholds; on/off and ground-follow gains; foot locking
and placement toggles. The [DS1R definition][p1-skeleton] corroborates the field
types, including numerous signed 16-bit values.

The wiki has inconsistent units: cosine fields described as degrees, metre
descriptions alongside centimetre fields, and packed integer data. Do not
convert these into live settings without checking their scaling/consumer.
Nothing here establishes native arm reach, shoulder sockets, avatar mesh
resizing or permission to scale colliders/bone bases. Foot IK also has timeline
enable/disable flags, so a PARAM value alone is not necessarily the active mode.

[MoveParam][move] maps standing/walking/dashing/rolling/turning and other motions
to animation IDs and includes `analogMoveParamId` for blending. It is not a
direct physical speed/capsule-size control. [RagdollParam][ragdoll] describes
hierarchy, velocity/position/acceleration, damping and snap gains plus a damage
enable flag and part mask; these do not identify the skinned render owner.

### The useful modern inspection route

[Soulstruct for Blender's pinned README][p4] claims full DSR import/export for
FLVER, collision, navigation, animations and MSB; it currently requires Blender
5.1+. This is the source snapshot's requirement, not a tested local installation.
It also documents:

- FromSoft Y-up/left-handed versus Blender Z-up/right-handed. Position conversion
  swaps Y/Z; its Euler recipe also negates components. **Do not apply an Euler
  recipe directly to native quaternion/matrix corrections.**
- Animated FLVER vertices require 1..4 bone weights; static map pieces require
  one. Armature/bone groups, material layouts and markers must survive a round trip.
- Model markers appear as Empty objects; reference IDs are encoded in their
  names. Blender parenting and the additional FLVER parent-bone property differ.
- Hi-/lo-res map collisions, collision material indices and navmesh connectivity
  are distinct from render meshes. MCP/MCG data refer to MSB navmesh parts.
- Animation import can interpolate 30-fps source data to a 60-fps Blender scene.
  Match scene rate to import settings rather than mistaking playback speed.
- A separate project directory is supported. `Also Export to Game` can write the
  installation: leave it off for research and use protected copies if testing.

This gives a concrete way to inspect armor weights, attachment markers and
native geometry **without guessing runtime ownership**, but no model was
imported/exported and no armor/headset behavior was qualified in this research.

## 4. File formats and world/scripting resources

| Family | Best cached entry point / distinction |
|---|---|
| BND / BDT / BHT / DCX | [Format index][formats], [DCX][dcx], [SoulsFormatsNEXT][p3]. Containers, archive indexes/data and compression are not interchangeable payloads; several wiki format pages are only headings. |
| PARAM / PARAMBND | Game row schemas and bundled parameter resources; use the DS1R Paramdex directory. The hub's generic descriptions contain copy/paste from other games. |
| FLVER / MTD / TPF / DDS | Render model, material definitions, texture containers and encoded texture data. MTD/TPF wiki destinations did not return raw content; cached upstream parser source and Blender documentation fill part of that gap. |
| ANIBND / HKX / TAE | Animation bundle, Havok data and timed events. `.hkx` can mean animation, collision or another Havok asset: extension alone is insufficient. HKX's wiki destination was not a usable spec. |
| ESD | Action/reaction and dialogue state machines, not the map event scripting language. |
| EMEVD | [Map/world event scripts][emevd], with flags, conditions and results. See [DS1 commands][commands] and [tutorial][emevd-tutorial]. `common.emevd` and map-specific scripts serve different scopes. |
| MSB / NVM / MCG / MCP | Map placements, navigation surfaces, navigation graph and linked boxes/connectivity. The Blender README is substantially richer than missing wiki format pages. |
| FMG / DRB | [Text tables][fmg], [common FMG-name list][fmg-names], UI/layout resources. Text IDs are not equipment or event IDs just because the integers overlap. |
| FFX / FXR | [DS1 FFX page][ffx], particles and resource bundles. Do not import later-game FXR assumptions into DS1/DSR. |
| FEV / FSB | Sound metadata/banks; tools index lists DS1/DSR SoundSouls and legacy FMOD routes. No proprietary FMOD/Havok tool bundle was fetched. |
| SL2 | [Save format overview][sl2]. Research does not authorize editing personal saves or weakening our protected-copy policy. |

### World and content ID references

The hub's [character list][characters], [part list][parts], [objects][objects],
[map names][maps], [boss flags][bosses] and [fogwall IDs][fogwalls] give mutually
useful annotation sets. A character model ID (`cXXXX`), part model, map entity
ID, boss event flag, weapon PARAM ID and live actor handle are **not** the same
identity namespace. Keep that distinction in fixtures and traces.

External [event flags][flags] resolve to Wulf2k's NamedIDs and LukeLavan's FLAGS.py;
both source texts are cached without executing either. The following linked
workbooks contain considerably more than their wiki landing pages:

| Workbook source | Retained tabs (serialized rows, including headers) |
|---|---|
| [Character states][states] | Active 278; passive 226. |
| [Cutscenes][cutscenes] | Introduction 25; Remo Codes **941**; Map List 19; debug-menu tutorial 10; troubleshooting 10. The initial linked CSV was only a cover tab. |
| [Object sheet][object-sheet] | List **561**; Sheet1 134. |
| [DS1 particles][particles] | FFX List **1007**; m18 4; Notes 5. The initial CSV contained only 320 rows, so the whole workbook matters. |
| [DS1 sound sheet][sounds] | Intro 19; Complete Dialog List **3387**; Music/SFX **2904**; Cutscenes 90; Ambience 10; Japanese dialogue FMG 3325. |
| [TeamTypes][teams] | Introduction, two methods, DS1 Table, DS2/DS3/Sekiro/Elden Ring tables and data sources. **The DS1 Table is effectively empty**; other-game team behavior is not DSR evidence. |

Workbook exports preserve text/cached values, not the complete appearance,
comments, videos or live calculation semantics. Original XLSX files are kept
alongside derived CSV tabs. Cross-game sheets remain labeled as such.

The [common Lua functions][lua] and [DS1 AI function repository][lua-common]
provide goals, target categories, timers, number slots, event-request interfaces
and combo/OnIf patterns. The [Havok references][havok] / [script page][hks] are
conceptual cross-game material; their HKS/generator recipes do not establish
that DS1's action ESD is replaceable by a later game's behavior graph.

## 5. DSR-specific rendering reference

[DSR texture map types][textures] are unusually valuable because the page is
explicitly DSR-specific rather than a generic DS1 redirect:

| Sampler / suffix | Community-described channels |
|---|---|
| `g_Diffuse`, no suffix | RGB base color; alpha transparency for TexEdge/Blend materials. |
| `g_Bumpmap`, `_n` | R/G encode normal X/Y from -1..1 into 0..1; B/A unused. |
| `g_Specular`, `_s` | R roughness, G metalness, B DiffuseF0 (0..0.08 packed into 0..1), A emissive. **Not just RGB specular color.** |
| `g_Lightmap`, `_lit` / `_l` | RGB baked light/shadow; alpha unknown. |
| `g_DetailBumpmap`, `_n` | R/G detail normal components; B/A unused. |
| `g_Subsurf`, `_t` | R strength, G thickness; B/A unused. |
| `g_Height`, `_h` | RGB height; A unused. |

The wiki also lists material-name tokens D/B/S/L/DetB/T/H. Preserve sampler
binding, UV layout and DDS compression as well as pixels; otherwise a converted
model can look wrong without a skinning error. This table is not a verified
shader disassembly or stereo-render contract.

[LockCamParam][lockcam] includes target distance, X rotation bounds, camera
look-height offset, lock radius and vertical FOV. Those are native camera-data
leads, **not an XR projection, IPD or native stereo-eye interface**. The graphics
PARAM tables cover light/fog, scattering, lens flare, shadow, tone mapping, LOD,
environment textures and DOF; use DS1R definitions especially for tone maps.

## 6. Tool map and compatibility traps

These are source-documented capabilities, **not tools installed or tested here**.
See [wiki tool index][tools] and pinned upstream READMEs.

| Need | Candidate / source | Important limitation |
|---|---|---|
| PARAM, map, text inspection | [Smithbox][p7], [Soulstruct][p8] | Select DSR project/schema; maintain a separate project copy. Generic "All" support labels are not a version guarantee. |
| Typed binary parsing | [SoulsFormatsNEXT][p3] | Library implementation is evidence for a file format, not native memory ABI. |
| TAE, attack windows, dummy visualization | [DSAnimStudio][p2] | DSR editing is documented; viewer support has caveats. README says an old animation downgrade step is no longer needed. Never impose old PTDE conversion advice by habit. |
| FLVER, skeleton, HKX and collision inspection | [Soulstruct for Blender][p4] | Current README requires Blender 5.1+; its coordinate and weight constraints matter. |
| ESD decompilation/compilation | [ESDLang][p6]; Zeditor via wiki | Distinguish action ESD from talk ESD; do not execute decompiled `.py` as Python. Back up and verify round trips. |
| EMEVD | Darkscript 3 / Soulstruct | World scripting is not a direct native melee binding. |
| Container unpack/repack | WitchyBND | Tutorial index says old Yabber workflows are outdated. A replacement tool still needs exact-game round-trip checks. |
| Archive extraction | Nuxe / UXM Selective | Wiki says Nuxe supersedes UXM except PTDE; extraction/EXE patching is not part of this research authorization. |
| Loose-file mod loading | DSR-specific Mod Engine route | Tool index contains both DSR-specific and broad ModEngine entries. Confirm the actual fork/release supports our game and coexistence with DSVR's loader; do not replace `dinput8.dll` casually. |
| Live parameter reload / debug | DS1RParamReloader / DSR Gadget / debug menu | Can change runtime state; debug-menu page mentions downpatching. None was installed or used for research. |
| DDS comparison | DirectXTex Texdiag, texture editors | Compare the original resource's format; changing extension is not conversion. |
| Audio | DS1/DSR SoundSouls, legacy FMOD | References include legacy/proprietary archives. Only links/metadata were retained. |

The [tutorial index][tutorials] explicitly labels games and outdated tools.
"Adding a Weapon", paired/trick weapons and Havok-variable tutorials in the
general crawl can be DS3/ER-oriented despite a title without the game name.
Use the tutorial's parent section and actual text, not keyword matching, to
decide applicability. The [DS1 spell overview][spells], [new DS1 spells guide][new-spells],
[DS1 map-texture guide][obj-textures], [bonfire basics][bonfires],
[DS1 breakobj generation][breakobj] and [Soulstruct setup][soulstruct-tutorial]
are better targeted starting points. No steps from those guides were applied.

## 7. Complete parameter-family navigation

All the following wiki tables were cached with their field types, row offsets,
descriptions and available enums. Blank enum sections/unknown descriptions stay
unknown. PARAMDEF names sometimes differ from wiki/table names (for example
`EnemyStandardInfo` vs EnemyBehaviorBank, `SpEffect` vs SpEffectParam).

| Area | Cached tables |
|---|---|
| Attack and action | [AtkParam][atk], [BehaviorParam][behavior], [Bullet](https://soulsmodding.com/doku.php?id=ds1-refmat:param:bullet), [ThrowParam][throw], [KnockBackParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:knockbackparam), [HitMtrlParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:hitmtrlparam). |
| Equipment | [EquipParamWeapon][weapon], [EquipParamProtector](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamprotector), [EquipParamAccessory](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamaccessory), [EquipParamGoods](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamgoods), [EquipMtrlSetParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipmtrlsetparam), [ReinforceParamWeapon](https://soulsmodding.com/doku.php?id=ds1-refmat:param:reinforceparamweapon), [ReinforceParamProtector](https://soulsmodding.com/doku.php?id=ds1-refmat:param:reinforceparamprotector). |
| Actors / animation / physics | [CharaInitParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:charainitparam), [NpcParam][npc], [NpcThinkParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:npcthinkparam), [AIStandardInfoBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:aistandardinfobank), [EnemyBehaviorBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:enemybehaviorbank), [SkeletonParam][skeleton], [MoveParam][move], [RagdollParam][ragdoll]. |
| Status, curves, spell context | [SpEffectParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:speffectparam), [SpEffectVfxParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:speffectvfxparam), [CalcCorrectGraph](https://soulsmodding.com/doku.php?id=ds1-refmat:param:calccorrectgraph), [Magic](https://soulsmodding.com/doku.php?id=ds1-refmat:param:magic), [LevelSyncParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:levelsyncparam), [CoolTimeParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:cooltimeparam), [WhiteCoolTimeParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:whitecooltimeparam). |
| World/content/rewards | [GameAreaParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:gameareaparam), [ItemLotParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:itemlotparam), [ShopLineupParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:shoplineupparam), [ObjActParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:objactparam), [ObjectParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:objectparam), [TalkParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:talkparam), [FaceGenParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:facegenparam), [MenuColorTableParam](https://soulsmodding.com/doku.php?id=ds1-refmat:param:menucolortableparam). |
| Camera and inherited/legacy concepts | [LockCamParam][lockcam], [QwcChange](https://soulsmodding.com/doku.php?id=ds1-refmat:param:qwcchange), [QwcJudge](https://soulsmodding.com/doku.php?id=ds1-refmat:param:qwcjudge). Presence/name alone does not establish an active DS1 world-tendency system. |
| Rendering | [DofBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:dofbank), [EnvLightTexBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:envlighttexbank), [FogBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:fogbank), [LensFlareBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:lensflarebank), [LensFlareExBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:lensflareexbank), [LightBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:lightbank), [LightScatteringBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:lightscatteringbank), [LodBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:lodbank), [PointLightBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:pointlightbank), [ShadowBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:shadowbank), [ToneMapBank](https://soulsmodding.com/doku.php?id=ds1-refmat:param:tonemapbank). |

Additional direct references include [SpEffect state-info labels](https://soulsmodding.com/doku.php?id=ds1-refmat:speffect-state-info-list),
[vanilla correction curves](https://soulsmodding.com/doku.php?id=ds1-refmat:vanilla-calccorrectgraph),
[infusion approximations](https://soulsmodding.com/doku.php?id=ds1-refmat:weapon-infusion-modifier-values),
[shop ranges](https://soulsmodding.com/doku.php?id=ds1-refmat:vanilla-shops),
[unused VFX](https://soulsmodding.com/doku.php?id=ds1-refmat:unused-vfx-list) and
[loading-screen executable edits](https://soulsmodding.com/doku.php?id=ds1-refmat:loading-screen-items).
Labels such as "Hill", "Soul coins" and ambiguous legacy properties show why
these tables must not be treated as polished executable specifications.

## 8. Using and verifying the local cache

Root: `build/research/soulsmodding-20261024/` (ignored, private local material).

| Path below root | Purpose |
|---|---|
| `INDEX.md` | Generated page inventory with status, source URL, local filename and size. |
| `manifest.json` | Wiki status, original/raw URL, retrieval time, SHA256, headings and outbound links. |
| `raw/*.wiki` | Complete raw exports. `:` is encoded as `__` in filenames. HTML fallback files remain distinguishable in the manifest. |
| `tables.tsv` | Searchable page-tagged wiki table rows; not a normalized database or independently verified schema. |
| `primary/manifest.json` | Initial upstream file manifests, commits and individual sheet export results. |
| `primary/supplement-manifest.json` | Extra source/name/metadata files and complete-workbook tab manifests. |
| `primary/paramdex-comparison.json` | Exact ordered-field comparison described above. |
| `primary/<owner>__<repo>/files/` | Pinned source/XML/README snapshots; never imported or executed. |
| `primary/workbooks/<sheet-id>/` | Original public XLSX plus each tab as CSV. |
| `linked/manifest.json` | Additional linked docs, animations and event-flag source hashes. |
| `verification.json` | Local hash, parse, coverage and count checks. |

Examples from repository root:

```powershell
rg -n 'DmyPoly|hitSourceType|disableGuard' build/research/soulsmodding-20261024/raw
rg -n 'Parry|Guarding|Foot IK' build/research/soulsmodding-20261024/primary/Meowmaritus__DSAnimStudio/files
rg -n 'Ripost|Parr|Guard' build/research/soulsmodding-20261024/primary/workbooks
rg -n '^1401000 |^500000 ' build/research/soulsmodding-20261024/primary/soulsmods__Paramdex/files/DS1R/Names
python -B build/research/soulsmodding-20261024/verify_and_index.py
```

The local harvester and collectors are research receipts, not a new build
dependency. Do not rerun the collectors merely to read the cached results.
**`build/` is not backed up by Git**: this note and source links survive a clean
checkout, the complete snapshots do not. Preserve the directory separately if
cleaning build artifacts. A missing cache is an evidence gap, not permission to
pretend its full contents were retained in this note.

## Attribution, source pins and unresolved caveats

Souls Modding Wiki contributors own the community documentation; its footer
states [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) except
where otherwise noted. Wiki-derived summaries in this note are attributed to
the linked pages and are offered under that same license; this does **not**
change DSVR source-code licensing. Follow each page's history for author/revision
attribution. This research rephrases/selects facts rather than copying the site
into the distributable mod. Sheets and upstream projects have their own authors
and terms; do not assume the wiki license covers externally hosted material.
Sound sheet credits Demon's Lantern/Illusory Wall; cutscene sheet Challenger
Andy; AI repository king bore haha/george and contributors; linked dummy guide
Halvard; DDS guide Challenger Andy; Havok fundamentals Igor.

Primary source pins (full files/hashes in the private manifests):

| Ref | Repository | Commit |
|---|---|---|
| P1 | soulsmods/Paramdex | `ff7245e524329bc3eab00036723d2bd53384cedf` |
| P2 | Meowmaritus/DSAnimStudio | `f1bff06cd422de991b0a0fa8a2da81db43417318` |
| P3 | soulsmods/SoulsFormatsNEXT | `9e1c36f54962f80c143e3c1fe8a709204aefacf9` |
| P4 | Grimrukh/soulstruct-blender | `548afaae652ece0a6eac30b125b5e60cbd7bee00` |
| P5 | Xenthalos/ds1_chresd_metadata | `4287bc5a03100f44ff4aa19c44d92f3c12dbb885` |
| P6 | thefifthmatt/ESDLang | `9fa7fa90849a662b36839f077e321a02f9e992f4` |
| P7 | vawser/Smithbox | See cached commit receipt and pinned README URL in supplement manifest. |
| P8 | Grimrukh/soulstruct | `12b69189a2ccebbc623a1b6565be89a18d6c9958` |

Unresolved contradictions worth retaining: AtkParam's generic damage-level enum
labels differ from the dedicated stun page; SkeletonParam unit descriptions are
inconsistent; the ESD wiki includes an unversioned absolute dispatch address;
the hub's CharaInitParam text says "Armored Core" and some descriptions mention
affinities. **Do not reconcile these by guessing.** Prefer the exact DS1R
schema, pinned asset data and native consumer evidence. No wiki address was
installed into DSVR and no game test was launched for this research.

[hub]: https://soulsmodding.com/doku.php?id=ds1r-refmat:main
[atk]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:atkparam
[behavior]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:behaviorparam
[weapon]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:equipparamweapon
[throw]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:throwparam
[npc]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:npcparam
[stun]: https://soulsmodding.com/doku.php?id=ds1-refmat:stun-and-damage-level-info
[area]: https://soulsmodding.com/doku.php?id=ds1-refmat:area-scaling-speffects
[tae]: https://soulsmodding.com/doku.php?id=format:tae
[motion]: https://soulsmodding.com/doku.php?id=ds1-refmat:wepmotioncategory-list
[anim-schema]: https://soulsmodding.com/doku.php?id=ds1-refmat:attack-animation-schema
[anim-list]: https://darksoulsdebug.wikidot.com/animation-ids
[esd]: https://soulsmodding.com/doku.php?id=format:esd
[esd-states]: https://soulsmodding.com/doku.php?id=format:esd-state-list
[states]: https://docs.google.com/spreadsheets/d/1wnvL4O-6-Y6EzMQiIEuf_-dVTDzMl6y2YwOj4tk4t9o/edit
[flver]: https://soulsmodding.com/doku.php?id=format:flver
[skeleton]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:skeletonparam
[move]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:moveparam
[ragdoll]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:ragdollparam
[formats]: https://soulsmodding.com/doku.php?id=format:main
[dcx]: https://soulsmodding.com/doku.php?id=format:dcx
[emevd]: https://soulsmodding.com/doku.php?id=format:emevd
[commands]: https://soulsmodding.com/doku.php?id=ds1-refmat:emevd-commands
[emevd-tutorial]: https://soulsmodding.com/doku.php?id=tutorial:learning-how-to-use-emevd
[fmg]: https://soulsmodding.com/doku.php?id=format:fmg
[fmg-names]: https://soulsmodding.com/doku.php?id=common-refmat:fmg_names
[ffx]: https://soulsmodding.com/doku.php?id=format:ffx
[sl2]: https://soulsmodding.com/doku.php?id=format:sl2
[characters]: https://soulsmodding.com/doku.php?id=ds1-refmat:character-list
[parts]: https://soulsmodding.com/doku.php?id=ds1-refmat:part-list
[objects]: https://soulsmodding.com/doku.php?id=ds1-refmat:object-list
[maps]: https://soulsmodding.com/doku.php?id=ds1-refmat:map-name-list
[bosses]: https://soulsmodding.com/doku.php?id=ds1-refmat:boss-info
[fogwalls]: https://soulsmodding.com/doku.php?id=ds1-refmat:fogwall-ids
[flags]: https://soulsmodding.com/doku.php?id=ds1-refmat:event-flag-list
[cutscenes]: https://docs.google.com/spreadsheets/d/1r_u4pAEZyRRwQpsEP4EWw84eWDQ8LK2GuCcUFRBSvrg/edit
[object-sheet]: https://docs.google.com/spreadsheets/d/1FPMUEOP98OlwFtEeZns7-Dd248aZf3LVWfTGwpbH5K8/edit
[particles]: https://docs.google.com/spreadsheets/d/1QUlCf9NG_A1OzqYF8uaaRV6Xr0MLTDuXcZk74rfAPM4/edit
[sounds]: https://docs.google.com/spreadsheets/d/1gkRkvd4sJrDzjNgD0dgW5WdHAytlB4znIfz4kq-WVH4/edit
[teams]: https://docs.google.com/spreadsheets/d/10KecK-XcZjyrZrWHgx4dq5XG4q54lLfv1jmU-AXNgN0/edit
[lua]: https://soulsmodding.com/doku.php?id=common-refmat:lua_functions
[lua-common]: https://soulsmodding.com/doku.php?id=common-refmat:lua_ai_common_function_repository
[havok]: https://soulsmodding.com/doku.php?id=common-refmat:havok_behavior_reference
[hks]: https://soulsmodding.com/doku.php?id=common-refmat:havok_behavior_script
[textures]: https://soulsmodding.com/doku.php?id=ds1r-refmat:texture-maps
[lockcam]: https://soulsmodding.com/doku.php?id=ds1-refmat:param:lockcamparam
[tools]: https://soulsmodding.com/doku.php?id=tool:main
[tutorials]: https://soulsmodding.com/doku.php?id=tutorial:main
[spells]: https://soulsmodding.com/doku.php?id=tutorial:new-spells-overview-in-ds1
[new-spells]: https://soulsmodding.com/doku.php?id=tutorial:how-to-create-new-spells-in-ds1
[obj-textures]: https://soulsmodding.com/doku.php?id=tutorial:how-to-make-object-textures-load-in-any-map-ds1
[bonfires]: https://soulsmodding.com/doku.php?id=tutorial:ds1-bonfire-basics
[breakobj]: https://soulsmodding.com/doku.php?id=tutorial:ds1-breakobj-generation
[soulstruct-tutorial]: https://soulsmodding.com/doku.php?id=tutorial:ds1-modding-with-soulstruct
[p1]: https://github.com/soulsmods/Paramdex/tree/ff7245e524329bc3eab00036723d2bd53384cedf/DS1R
[p1-atk]: https://github.com/soulsmods/Paramdex/blob/ff7245e524329bc3eab00036723d2bd53384cedf/DS1R/Defs/AtkParam.xml
[p1-skeleton]: https://github.com/soulsmods/Paramdex/blob/ff7245e524329bc3eab00036723d2bd53384cedf/DS1R/Defs/SkeletonParam.xml
[p1-names]: https://github.com/soulsmods/Paramdex/blob/ff7245e524329bc3eab00036723d2bd53384cedf/DS1R/Names/EquipParamWeapon.txt
[p2]: https://github.com/Meowmaritus/DSAnimStudio/blob/f1bff06cd422de991b0a0fa8a2da81db43417318/README.md
[p2-tae]: https://github.com/Meowmaritus/DSAnimStudio/blob/f1bff06cd422de991b0a0fa8a2da81db43417318/DSAnimStudioNETCore/Res/TAE.Template.DS1.xml
[p2-behavior]: https://github.com/Meowmaritus/DSAnimStudio/blob/f1bff06cd422de991b0a0fa8a2da81db43417318/DSAnimStudioNETCore/ParamData/ParamData.BehaviorParam.cs
[p2-dummy]: https://github.com/Meowmaritus/DSAnimStudio/blob/f1bff06cd422de991b0a0fa8a2da81db43417318/DSAnimStudioNETCore/NewDummyPolyManager.cs
[p3]: https://github.com/soulsmods/SoulsFormatsNEXT/tree/9e1c36f54962f80c143e3c1fe8a709204aefacf9
[p3-dummy]: https://github.com/soulsmods/SoulsFormatsNEXT/blob/9e1c36f54962f80c143e3c1fe8a709204aefacf9/SoulsFormats/Formats/FLVER/Dummy.cs
[p4]: https://github.com/Grimrukh/soulstruct-blender/blob/548afaae652ece0a6eac30b125b5e60cbd7bee00/README.md
[p5]: https://github.com/Xenthalos/ds1_chresd_metadata/blob/4287bc5a03100f44ff4aa19c44d92f3c12dbb885/README.md
[p6]: https://github.com/thefifthmatt/ESDLang/blob/9fa7fa90849a662b36839f077e321a02f9e992f4/README.md
[p7]: https://github.com/vawser/Smithbox
[p8]: https://github.com/Grimrukh/soulstruct/blob/12b69189a2ccebbc623a1b6565be89a18d6c9958/README.md
