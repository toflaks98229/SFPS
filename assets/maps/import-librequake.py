#!/usr/bin/env python3
"""Convert one LibreQuake deathmatch map into a level this game can load.

    python import-librequake.py lqdm13            # prints a report, writes nothing
    python import-librequake.py lqdm13 --emit     # writes lqdm13.map beside this
    python import-librequake.py lqdm13 --from x.map --emit    # from a local copy

THE SOURCE IS THE NETWORK UNLESS --from SAYS OTHERWISE, and that is not a
preference. This script writes `<name>.map` into the directory it runs in, which
is the same directory the converted map lives in -- so a version that fell back
to reading `<name>.map` when it was there would, on its second run, convert its
own output. The furniture would find no deathmatch starts left to stand on, the
drops would find nothing left to drop, and the map would quietly lose its
spawners. An importer that is not safe to run twice is an importer nobody dares
run once.

AN AUTHORING TOOL THE BUILD DOES NOT RUN, exactly like import-doom-level.py and
the sprite importers. The .map it writes is committed, so a checkout needs
neither Python nor a network; this file is kept because that map is the RESULT
of a conversion and this is the recipe.

WHY THIS IS POSSIBLE NOW AND WAS NOT BEFORE
-------------------------------------------
import-doom-level.py's own docstring says Quake maps "share no structure with
this engine at all". That was true when it was written and has not been true
since brush.c landed: this game reads Valve 220 and Standard .map text directly,
which is the format TrenchBroom saves and the format LibreQuake ships. The
sentence outlived what it was reasoning about; it is corrected there.

WHY LibreQuake AND NOT THE OTHERS
---------------------------------
The trap README.md states: an engine's licence is not its assets' licence. It
holds for maps exactly as it holds for sprites.

  LibreQuake        docs/COPYING is BSD-3 project-wide, and
                    docs/README-IMPORTANT-LICENCE-INFO carves GPL-2 out for the
                    QuakeC, progs.dat and pop.lmp ONLY. The half we take from is
                    the permissive one.  <- chosen

                    THE MAPS ARE PERMISSIVE BY RESIDUE, NOT BY ENUMERATION, and
                    the distinction is worth carrying because it is what a
                    paraphrase loses. That carve-out document names the BSD-3
                    side as "models, textures and sounds"; the word "maps" is on
                    NEITHER side of it. Maps are BSD-3 because everything not
                    carved out to GPL-2 is covered by the project-wide COPYING,
                    and because docs/CREDITS lists Maps among the contributions
                    the project received. That is a sound chain. It is not the
                    same chain as "the licence file says maps", and a reader who
                    checks GitHub's licence badge will be told NOASSERTION.
  OpenArena         GPL for code AND assets. This game bakes its maps INTO the
  Xonotic           executable, so copyleft on a map is copyleft on the whole
                    binary. README.md already rejected both, for sprites, on
                    this exact reasoning.
  spirit-quake-maps GPL-2 throughout. Same verdict, same reason.
  id's own Quake    Refused on the LICENCE, not on absence -- they exist. John
                    Romero released the original .map sources on 11 Oct 2006:
                    e1m1-e1m8, e2m1-e2m7 plus the cut e2m10, e3m1-e3m7,
                    e4m1-e4m8, start, end, dm1..dm8 and 22 b_*.MAP item
                    brushmodels. An earlier version of this table said "never
                    released", which was simply wrong; the version after it
                    said "dm1..dm6", which was wrong by two.

                    AND IT IS A DOUBLE BIND, which is worth stating because it
                    is a stronger exclusion than "GPL-2" alone. The blog post
                    carried no licence text at all; the GPL was an amendment
                    days later, the version is stated nowhere by Romero, and
                    the re-uploaded archive lacks gpl.txt again -- so the
                    artefact most people hold asserts "id Software (C) 1996"
                    and grants nothing. Either the grant is valid, and these
                    are GPL-2 and copyleft reaches the binary; or it is not --
                    he had been ten years gone from id and neither id nor
                    ZeniMax nor Microsoft has ever restated it -- and they are
                    unlicensed, which is all rights reserved. There is no
                    third reading. Both roads end here.

                    CORROBORATED BY THE BEST AVAILABLE SOURCE: LibreQuake's own
                    maintainers examined exactly this question in their issue
                    #23 and declined to use Romero's maps. The project whose
                    maps this game does take from looked at the ones it does
                    not, and reached this verdict first.

  TWO PERMISSIVE LABELS THAT ARE NOT PERMISSIVE CONTENTS, recorded because the
  shape will recur and a search engine hands you the label, not the licence:
    jdolan/quetoo-data       repo DESCRIPTION says "Creative Commons
                             Attribution licence"; LICENSE.md is CC-BY-SA-4.0.
                             ShareAlike reaches a baked binary exactly as GPL
                             does. It is otherwise the best non-LibreQuake
                             pool of .map sources in existence, 38 of them.
    quake-leveldesign-       GitHub's badge says CC0-1.0. The CC0 covers the
    starterkit               kit; the maps inside are Romero's 2006 release
                             above. A permissive wrapper around GPL contents.

WHAT DOES NOT SURVIVE THE CROSSING
----------------------------------
A Quake DM map is a room for a game with armour, eight weapons and no waves.
This engine has none of the first two and is built on the third, so the
conversion is not lossless and does not pretend to be:

  * TELEPORTERS CROSS NOW, and this line used to say they were dropped. The
    engine grew a TeleportDef -- a trigger volume with a place instead of a tag
    -- so `trigger_teleport` and the `info_teleport_destination` it names both
    survive, and the route they make survives with them. What still does NOT
    cross is a teleporter driven by a relay rather than by walking into it;
    trigger_relay is in DROP and nothing here fires a tag at a teleporter.
  * ARMOUR BECOMES HEALTH, except the red suit, which becomes a timed cut to
    the damage taken. There is still no second damage pool in this game --
    that is the point: the pool became a clock rather than a second bar.
    See player.h's PowerKind.
  * EIGHT WEAPONS BECOME FOUR. The mapping below is by ROLE -- hitscan spread,
    sustained fire, splash -- not by name.
  * SKY BECOMES A CEILING. Quake sky brushes are solid and drawn as sky; this
    engine has no sky pass, so they are drawn as what they physically are.

WHAT THE MAP GAINS
------------------
A LEVEL WITH NO SPAWNERS IS NOT AN ARENA. world.c is explicit that "a level that
is an arena becomes one by having spawners rather than by being named", and a DM
map has none -- it was built for other players. Imported untouched it would be a
room with no game in it.

So the spawners and the shrine are placed AT THE DEATHMATCH STARTS this
conversion is otherwise dropping. That is not a convenience: an
info_player_deathmatch is, by construction, a point the map's author verified a
player can stand at -- clear of geometry, on a floor, inside the level. Nothing
else in the file carries that guarantee, and picking coordinates by hand out of
a 246-brush map means picking some of them inside a wall.

The result is a DRAFT, the same way import-doom-level.py's output is. Which
spawner goes where, how fast, and where the shrine sits are tuning questions
that want somebody with the game running.
"""
import os
import re
import sys
import urllib.request

SRC = ('https://raw.githubusercontent.com/lavenderdotpet/LibreQuake/main/'
       'lq1/maps/src/dm/%s.map')

# --- what the engine will not exceed ----------------------------------------
#
# Checked here rather than discovered as a level that loads with its far half
# missing. brush.h and level.h own these numbers.
#
# READ FROM THE HEADERS, NOT COPIED FROM THEM. The previous version of this
# table carried the values inline under a comment saying "a copy that drifts is
# a copy that reports the wrong cap" -- and then the caps were raised and the
# copy was not, so for a whole branch this script measured every map against
# BR_MAX_BRUSHES 512 and BR_MAX_ENTS 96 while the engine had 1024 and 192. It
# reported maps as too big that fit. A comment warning about drift does not
# prevent drift; reading the one source of truth does.
#
# 헤더에서 *읽습니다.* 복사하지 않습니다. 이 표의 이전 판은 "드리프트한 사본은 틀린 상한을
# 보고하는 사본"이라는 주석 아래에 값을 인라인으로 지니고 있었습니다. 그리고 상한이 올라갔는데
# 사본은 올라가지 않았고, 그래서 한 브랜치 내내 이 스크립트는 엔진이 1024와 192를 가진 동안
# 모든 맵을 BR_MAX_BRUSHES 512와 BR_MAX_ENTS 96에 대조해 쟀습니다. 들어가는 맵을 너무 크다고
# 보고했습니다. 드리프트를 경고하는 주석은 드리프트를 막지 못합니다. 진실의 단일한 출처를 읽는
# 것이 막습니다.
CAP_HEADERS = ('../../src/brush.h', '../../src/level.h', '../../src/enemy.h')

CAP_NAMES = {
    'brushes':    'BR_MAX_BRUSHES',
    'entities':   'BR_MAX_ENTS',
    'faces/brush':'BR_MAX_FACES',
    'level ents': 'LVL_MAX_ENTS',
    'lights':     'LVL_MAX_LIGHTS',
    'doors':      'LVL_MAX_DOORS',
    'triggers':   'LVL_MAX_TRIGGERS',
    'teleports':  'LVL_MAX_TELEPORTS',
    'hazards':    'LVL_MAX_HAZARDS',
    'ward cands': 'BOSS_MAX_CAND',
    'tex name':   'BR_TEX',
}


def read_caps():
    """Every cap in CAP_NAMES, read out of the headers that define them.

    REFUSES RATHER THAN GUESSES. A cap this cannot find is a cap this would
    otherwise report a made-up number for, and a made-up number in a report
    about capacity is worse than no report -- so a missing or non-integer
    define is an error naming the constant, not a fallback.
    찾지 못하면 *추측하지 않고 거부합니다.* 찾을 수 없는 상한은 이 스크립트가 지어낸 수를
    보고하게 될 상한이며, 용량에 대한 보고에서 지어낸 수는 보고가 없는 것보다 나쁩니다.
    그러므로 없거나 정수가 아닌 define은 대체값이 아니라 그 상수를 지목하는 오류입니다.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    text = ''
    for rel in CAP_HEADERS:
        path = os.path.join(here, *rel.split('/'))
        with open(path, encoding='utf-8') as f:
            text += f.read()

    caps = {}
    for label, name in CAP_NAMES.items():
        m = re.search(r'^#define\s+%s\s+(\d+)\s*(?:/|$)' % re.escape(name),
                      text, re.M)
        if not m:
            raise SystemExit(
                'import-librequake: cannot read %s from %s.\n'
                'The cap tables moved or the define is no longer a plain '
                'integer. Fix this rather than hard-coding a number: a report '
                'measured against the wrong cap is what this lookup exists to '
                'prevent.' % (name, ' or '.join(CAP_HEADERS)))
        caps[label] = (name, int(m.group(1)))
    return caps


CAPS = read_caps()

# --- textures ---------------------------------------------------------------
#
# LibreQuake's WAD names on the left, this project's procedural materials on the
# right. Matched by ROLE rather than by colour: `wall_grey_c` is 78% of the
# faces in the map and is the surface the room is made of, so it gets the
# surface glasstower is made of.
#
# clip / trigger / skip are NOT in this table on purpose -- see NODRAW below.
TEXTURES = {
    # NOT A TYPO, AND THAT TOOK A FAILING TEST TO SEE. `lqdm4` names two faces
    # `med_cslbrk18_t*` where the wad it declares holds `med_csl_brk18_t*`, one
    # underscore apart, and correcting the map to match the wad is the obvious
    # move. It is also wrong: `med_csl_brk18_tb` is SIXTEEN characters and
    # ::BR_TEX is sixteen BYTES -- Quake's fifteen plus a nul, which is the same
    # limit LibreQuake's own author was working to. The short spelling is not a
    # slip, it is the name that fits, and substituting the long one here put two
    # truncated names into the level and turned tools/mapcap.c red.
    # So the map keeps its own word and the fix lives on the other side: the
    # texture importer looks the wad file up under the long name and writes it
    # out under the short one. See MEANT there.
    # *오타가 아니며, 그것을 알아보는 데 실패한 검사가 필요했습니다.* `lqdm4`는 면 둘을
    # `med_cslbrk18_t*`로 부르고 그것이 선언한 wad는 `med_csl_brk18_t*`를 갖고 있습니다.
    # 밑줄 하나 차이이고, 맵을 wad에 맞춰 고치는 것이 뻔한 수입니다. 그리고 그것은 틀렸습니다.
    # `med_csl_brk18_tb`는 *열여섯* 글자이고 ::BR_TEX는 열여섯 *바이트*입니다. Quake의 열다섯
    # 더하기 널이며, LibreQuake 제작자가 맞추고 있던 것과 같은 상한입니다. 짧은 철자는 실수가
    # 아니라 *들어가는* 이름이고, 이곳에서 긴 쪽으로 치환한 것은 잘린 이름 둘을 레벨에 넣고
    # tools/mapcap.c를 빨갛게 만들었습니다.
    # 그래서 맵은 자기 낱말을 지키고 수정은 반대편에 있습니다. 텍스처 임포터가 긴 이름으로
    # wad 파일을 찾아 짧은 이름으로 씁니다. 그곳의 MEANT를 참조하십시오.
    'wall_grey_c':  'wall_stone',
    'wall_grey_b':  'wall_rough',
    'floor_grey_c': 'wall_plain',
    'floor_red_c':  'wall_brick',
    'floor_red_a':  'prust',
    'black':        'wall_plain',
    # Solid in Quake, drawn as sky. There is no sky pass here, so it is drawn
    # as the ceiling it physically is -- onto the palest surface available,
    # for the reason spelled out at `snow1` below.
    'sky_void':     'wall_marble',
    'floor_blue_c':   'ptile',
    'floor_yellow_c': 'prust',
    # Liquid names -- the leading '*' is Quake's marker for one. Nothing in
    # textures.txt answers to any of them, and a material this engine cannot
    # resolve is not a failure it reports, so they are mapped rather than left
    # to find out. Teleport surfaces become a window: they are the one surface
    # in a Quake map that is supposed to look like it goes somewhere, and this
    # engine's only translucent-looking material is the closest thing to that
    # it has to say.
    # 액체 이름이며, 앞의 '*'가 그것에 대한 Quake의 표시입니다. 어느 것에도 textures.txt가
    # 답하지 않고, 이 엔진이 해석하지 못하는 재질은 그것이 보고하는 실패가 아니므로, 나중에
    # 알게 되도록 두지 않고 대응시킵니다. 텔레포트 표면이 창이 되는 이유는, 그것이 Quake 맵에서
    # 어딘가로 이어지는 것처럼 보여야 하는 유일한 표면이고 이 엔진이 가진 반투명해 보이는 유일한
    # 재질이 그에 대해 말할 수 있는 가장 가까운 것이기 때문입니다.
    '*tele3':       'pwindow',
    '*tele4':       'pwindow',
    '*teleport':    'pwindow',

}

# Names brush.c's own NODRAW list already answers to, so they cross unchanged
# and MEAN what they meant: a brush that collides and is not drawn. Listed here
# only so the report can tell "left alone on purpose" apart from "this script
# has never heard of it" -- and the second of those is the one that matters,
# because a material textures.txt cannot resolve is not a failure the engine
# reports. It is a surface, drawn as whatever the fallback happens to be.
# brush.c 자신의 NODRAW 목록이 이미 답하는 이름들이며, 그대로 건너와 원래의 뜻을 유지합니다.
# 충돌하되 그려지지 않는 브러시입니다. 이곳에 적는 이유는 보고서가 "일부러 그대로 둔 것"과
# "이 스크립트가 들어 본 적 없는 것"을 구별할 수 있게 하기 위해서뿐입니다. 그리고 중요한 것은
# 두 번째입니다. textures.txt가 해석하지 못하는 재질은 엔진이 보고하는 실패가 아니라, 폴백이
# 무엇이든 그것으로 그려지는 표면이기 때문입니다.
NODRAW = {'clip', 'trigger', 'skip', '__TB_empty'}

# Movers this engine has no counterpart for. They are NOT dropped -- their
# brushes are drawn and collided like every other brush in the file, because
# level.c passes 0..n_brushes to brush_geometry and brush_trace and never asks
# which entity owns one. So they arrive as STATIC geometry, frozen wherever the
# author left them.
#
# That is a real loss and it is reported rather than swallowed: a lift that does
# not lift is a route the player can see and cannot take, and it looks exactly
# like a lift that is broken. `func_door` is the only mover here, and mapping a
# plat onto it would be inventing a motion -- a Quake plat rises under you and a
# door here slides aside, which are not the same promise to a player standing on
# one.
#
# 이 엔진에 대응물이 없는 이동체입니다. *버리지 않습니다.* 그 브러시는 파일의 다른 모든 브러시와
# 같이 그려지고 충돌합니다. level.c가 brush_geometry와 brush_trace에 0..n_brushes를 넘기며 어떤
# 엔티티가 그것을 소유하는지 결코 묻지 않기 때문입니다. 그래서 제작자가 둔 자리에 얼어붙은
# *정적* 지오메트리로 도착합니다.
#
# 그것은 실제 손실이며 삼키지 않고 보고합니다. 올라가지 않는 리프트는 플레이어가 보면서 탈 수
# 없는 경로이고, 고장 난 리프트와 똑같아 보입니다. 이곳의 유일한 이동체는 `func_door`이며, 플랫을
# 그것에 대응시키는 것은 없는 움직임을 지어내는 일입니다. Quake의 플랫은 발밑에서 올라오고 이곳의
# 문은 옆으로 비켜서며, 그 위에 선 플레이어에게 그 둘은 같은 약속이 아닙니다.
FROZEN_MOVERS = {'func_plat', 'func_train', 'func_rotate_door', 'func_bob'}

# --- entities ---------------------------------------------------------------

# Brush containers a Quake compiler folds into the world. This engine already
# draws and collides every brush in the file regardless of which entity owns it
# -- level.c passes 0..n_brushes to brush_geometry and brush_trace -- so folding
# them costs nothing and buys back 42 of the 47 entities this map is over by.
MERGE_INTO_WORLD = {
    'func_detail', 'func_group', 'func_wall', 'func_illusionary',
    # ericw-tools' detail variants. Same idea, different hint to the compiler
    # about visibility and clipping -- all three are still just brushes, and
    # this engine has no compiler to hint. Missing them cost lqdm11 four
    # entities out of a budget it clears by two.
    # ericw-tools의 detail 변종들입니다. 같은 발상이고, 가시성과 클리핑에 대해 컴파일러에게 주는
    # 힌트만 다릅니다. 셋 다 여전히 그냥 브러시이고, 이 엔진에는 힌트를 줄 컴파일러가 없습니다.
    # 이것들을 빠뜨린 것이 lqdm11에게 엔티티 넷을 치르게 했습니다. 그 맵이 예산을 넘기지 않는
    # 여유가 둘인데 말입니다.
    'func_detail_wall', 'func_detail_illusionary', 'func_detail_fence',
}

# Dropped, with their brushes if they have any. Each one is a thing this game
# does not have rather than a thing this script could not be bothered with.
# Quake's lamp family, which this engine will not answer to by name.
#
# level.c matches `light` with txt_eq -- exact equality -- and its comment says
# why and predicts this exact moment: the length-compare it replaced "read
# `light` and accepted `light_fluoro`, `light_torch_small` and every other
# member of Quake's lamp family ... which the imported maps have not carried yet
# and would the first time one did". lqdm1 is the first that does, nineteen
# times, and left alone they are nineteen entities the engine steps over and the
# editor cannot name.
#
# THE CLASSNAME IS THE ONLY PARAMETER THEY CARRY. A Quake flame has no `light`
# and no `_color` key -- "small" and "large" are its reach and "white" and
# "yellow" are its colour, spelled into the name. So the mapping reads the name
# for what the keys would have said, and writes only what differs from what
# level.c already defaults to: white needs no `_color`, because white is what a
# light with none gets.
#
# The flame itself does not survive. There is no torch model here and no
# flickering light, so what arrives is the steady point light underneath one --
# the same crossing armour makes into health, and reported the same way.
#
# Quake의 등 계열이며, 이 엔진은 그 이름에 답하지 않습니다.
#
# level.c는 `light`를 txt_eq로, 즉 정확한 일치로 맞춥니다. 그 주석이 이유를 말하며 바로 이
# 순간을 예견합니다. 그것이 대체한 길이 비교는 "`light`를 읽으면서 `light_fluoro`,
# `light_torch_small`을 비롯한 Quake 등 계열 전부를 받아들였으며 ... 가져온 맵들이 아직 그것을
# 나르지 않았을 뿐이고 하나라도 나르는 순간 그렇게 된다"고 되어 있습니다. lqdm1이 그 첫
# 번째이며 열아홉 번 나릅니다. 그대로 두면 엔진이 지나치고 에디터가 이름 붙일 수 없는 엔티티
# 열아홉 개입니다.
#
# *classname이 그것들이 나르는 유일한 매개변수입니다.* Quake의 불꽃에는 `light` 키도 `_color`
# 키도 없습니다. "small"과 "large"가 도달 거리이고 "white"와 "yellow"가 색이며, 이름에 적혀
# 있습니다. 그래서 이 대응은 키가 말했을 것을 이름에서 읽고, level.c가 이미 기본값으로 삼는
# 것과 다른 것만 씁니다. 흰색에는 `_color`가 필요 없습니다. 그것이 없는 등이 얻는 색이 흰색이기
# 때문입니다.
LIGHTS = {
    'light_flame_small_white': {'light': '200'},
    'light_flame_large_yellow': {'light': '350', '_color': '255 220 140'},
}


# --- what the ENGINE can do with a classname --------------------------------
#
# THE DISPATCH ABOVE HAS A SILENT ARM. An entity that matches no table falls
# through to `kept` unchanged, and that reads as "crosses as itself" -- which is
# true of `worldspawn` and a lie about everything else. level.c takes a
# classname apart by ALIAS or by a `monster_`/`item_` PREFIX and IGNORES what is
# neither, in a loop whose own comment admits it "cannot check that this one is
# real". So an unmapped name is not carried across; it is DELETED, at load, with
# nothing said, in a different repository from the one that wrote it.
#
# Two things went out that way before this existed. `item_artifact_invulnerability`
# rode into the shipped map as an entity the engine has no name for. And the
# three artifacts THIS script converts were emitted as bare `quad`, `shadow` and
# `aegis` -- no prefix, so no kind, so no pickup: drawn, tested, documented and
# absent from the level.
#
# The rule is level.c's, restated here rather than guessed at, and the checker
# below is the only thing in either repository that compares the two.
#
# 한국어
# ------
# *위의 디스패치에는 조용한 갈래가 있습니다.* 어느 표에도 걸리지 않은 엔티티는 그대로 `kept`로
# 떨어지며, 그것은 "자기 자신으로 건너간다"로 읽힙니다. `worldspawn`에 대해서는 참이고 그 밖의
# 모든 것에 대해서는 거짓말입니다. level.c는 classname을 ALIAS나 `monster_`/`item_` *접두사*로
# 해체하고 둘 다 아닌 것은 *무시*하며, 그 루프의 주석은 "이것이 진짜인지 검사할 수 없다"고
# 스스로 인정합니다. 그러므로 매핑되지 않은 이름은 건너오는 것이 아니라 *삭제됩니다*. 로드
# 시점에, 아무 말 없이, 그것을 쓴 저장소가 아닌 다른 저장소에서 말입니다.
#
# 이것이 있기 전에 그렇게 나간 것이 둘입니다. `item_artifact_invulnerability`는 엔진이 이름을
# 모르는 엔티티로 출하 맵에 실려 갔습니다. 그리고 이 스크립트가 변환하는 아티팩트 셋은 접두사
# 없는 맨 `quad`, `shadow`, `aegis`로 나갔습니다. 접두사가 없으니 kind가 없고, kind가 없으니
# 획득물이 없습니다. 그려지고, 검사되고, 문서화되고, 레벨에는 없었습니다.
#
# 규칙은 level.c의 것이며 짐작이 아니라 이곳에 다시 적었습니다. 아래의 검사기는 두 저장소를
# 통틀어 그 둘을 비교하는 유일한 것입니다.
ENGINE_ALIASES = (
    'info_exit', 'info_push', 'info_altar', 'info_ward_air', 'info_ward_ground',
)
ENGINE_HANDLES = (
    'worldspawn', 'light', 'info_player_start', 'info_player_deathmatch',
    'trigger_teleport', 'info_teleport_destination', 'trigger_hurt', 'func_door',
)

def engine_understands(cn):
    """Whether this name reaching the .map means anything in the game.

    NOT just level.c's parse. That accepts ANY `item_x` and turns it into
    kind `x`, real or not -- which is how item_artifact_invulnerability rode
    in looking valid. The question worth asking is stricter: did this name
    come out of one of OUR tables, or is it one of the handful the engine
    handles by itself? Anything else is a name the source map had and this
    conversion never made a decision about.

    *level.c의 파싱만이 아닙니다.* 그것은 어떤 `item_x`든 받아 kind `x`로 만들며, 진짜인지는
    묻지 않습니다. item_artifact_invulnerability가 멀쩡해 보이며 실려 온 방식이 그것입니다.
    물을 값어치가 있는 질문은 더 엄격합니다. 이 이름이 *우리* 표에서 나왔는가, 아니면 엔진이
    스스로 처리하는 몇 안 되는 것 중 하나인가. 그 밖의 모든 것은 원본 맵에 있었고 이 변환이
    아무 결정도 내리지 않은 이름입니다."""
    return (cn in ENGINE_ALIASES or cn in ENGINE_HANDLES
            or cn.startswith('trigger_')
            or cn == 'light' or cn.startswith('light_')
            or cn in set(ITEMS.values())
            or cn in set(cn2 for cn2, _ in FURNITURE)
            or cn == MAW)

DROP = {
    'ambient_drip':               'a looping sound with no counterpart here',
    'info_intermission':          'a camera for a scoreboard this game has none of',
    'trigger_relay':              'fires a target; nothing here has targets',
    'trigger_once':               'likewise, and its brush would be a dead volume',

    # A LAMP LIGHTS NOTHING, so carrying one is bytes for nothing.
    #
    # scene.c's `scene_lights` says it outright -- `::Level::lights` is not read
    # there, "not from the vertices, not from these slots" -- and level.c's
    # bake_light says the other half. What lights this game is the sun in the
    # vertex colours and the things the player and the monsters put in the air.
    # A `light` entity is parsed, stored in `Level::lights`, counted against
    # LVL_MAX_LIGHTS, and then read by nothing except tools/lightprobe.c.
    #
    # DROPPED HERE RATHER THAN LEFT FOR THE ENGINE TO IGNORE, because the engine
    # ignoring them is not free: they are entities in the .map text that is baked
    # into the exe, they count against LVL_MAX_LIGHTS and can push a map over it,
    # and tools/scenetest.c asserts that no shipped level declares one.
    #
    # THIS TABLE IS WHY `lqdm1` HAD NONE. That map was stripped of its
    # thirty-two by hand when the lamps stopped lighting anything, and the
    # importer was not taught the same thing -- so re-running it would have put
    # them back, and the recipe stopped reproducing the map it made. `lqdm4`
    # arriving with fifty-two is what found that.
    #
    # *등은 아무것도 밝히지 않으므로*, 하나를 지니는 것은 아무것도 아닌 것에 바이트를 쓰는
    # 일입니다.
    #
    # scene.c의 `scene_lights`가 그것을 명시합니다. `::Level::lights`를 그곳에서 읽지
    # 않으며 "정점에서도, 이 슬롯에서도"입니다. 나머지 절반은 level.c의 bake_light가
    # 말합니다. 이 게임을 밝히는 것은 정점 색에 든 태양과, 플레이어와 몬스터가 공중에 놓는
    # 것들입니다. `light` 엔티티는 파싱되어 `Level::lights`에 저장되고 LVL_MAX_LIGHTS에
    # 대해 세어진 다음, tools/lightprobe.c 말고는 아무것도 읽지 않습니다.
    #
    # *엔진이 무시하도록 두지 않고 이곳에서 버리는* 이유는, 엔진이 무시하는 것이 공짜가
    # 아니기 때문입니다. 그것들은 exe에 구워지는 .map 텍스트 안의 엔티티이고,
    # LVL_MAX_LIGHTS에 대해 세어져 맵을 상한 너머로 밀 수 있으며, tools/scenetest.c가
    # 어떤 출하 레벨도 하나도 선언하지 않는다고 단언합니다.
    #
    # *이 표가 `lqdm1`에 등이 없던 이유입니다.* 그 맵은 등이 아무것도 밝히지 않게 되었을 때
    # 서른두 개를 손으로 걷어냈는데 임포터는 같은 것을 배우지 못했습니다. 그래서 다시
    # 실행하면 되돌아왔을 것이고, 레시피는 자기가 만든 맵을 재현하기를 그만둔 상태였습니다.
    # `lqdm4`가 쉰둘을 데리고 도착한 것이 그것을 찾아냈습니다.
    'light':                      'a lamp lights nothing; see scene_lights',
    # THE FOURTH ARTIFACT, and the one that does not cross. The other three
    # became clocks; invulnerability cannot, because a clock that sets damage
    # taken to zero is not a powerup this arena survives -- the lava sea is
    # the floor, and thirty seconds of walking through it unharmed is a
    # different level. It was not in this table before and it was not in any
    # other either, so it fell through UNMAPPED AND UNREPORTED and shipped
    # into lqdm4.map as an entity the engine has no name for.
    'item_artifact_invulnerability': 'no invulnerability; see player.h',
}

# Quake's roster against ours. By role: shells are the spread weapon's, spikes
# and cells feed sustained fire, rockets are splash.
ITEMS = {
    # `item_` IS NOT DECORATION. level.c takes a classname apart by ALIAS or by
    # a `monster_`/`item_` prefix and IGNORES everything else, so a bare `quad`
    # parsed to no kind at all and the artifact never reached the level. The
    # prefix is what makes the remainder the pickup name -- exactly how
    # `item_health` has always worked.
    # `item_`은 장식이 아닙니다. level.c는 classname을 ALIAS나 `monster_`/`item_` 접두사로
    # 해체하고 그 밖의 모든 것을 *무시*하므로, 맨이름 `quad`는 아무 kind로도 파싱되지 않았고
    # 아티팩트는 레벨에 닿은 적이 없습니다. 접두사가 나머지를 획득물 이름으로 만듭니다.
    # `item_health`가 늘 동작해 온 방식 그대로입니다.
    # THE THREE ARTIFACTS, and one of them is an adaptation. Quake's quad and
    # its ring of shadows are timers already and cross as themselves. Red armour
    # is a second damage POOL, which the note below still says this game does not
    # have -- so it crosses as what a suit of armour does for a while rather than
    # as a pool: PW_AEGIS, damage taken cut while a clock runs. See player.h.
    # *아티팩트 셋이며, 그중 하나는 각색입니다.* Quake의 쿼드와 그림자 반지는 이미
    # 타이머이므로 자기 자신으로 건너옵니다. 붉은 갑옷은 두 번째 피해 *풀*이고, 아래의
    # 문장이 이 게임에 그런 것이 없다고 여전히 말합니다. 그래서 풀이 아니라 *갑옷이 한동안
    # 해 주는 일*로 건너옵니다. PW_AEGIS이며, 시계가 도는 동안 받는 피해가 줄어듭니다.
    # player.h를 참조하십시오.
    'item_artifact_super_damage': 'item_quad',
    'item_artifact_invisibility': 'item_shadow',
    'item_armorInv':              'item_aegis',

    'item_health':           'item_health',
    'item_shells':           'item_shotgunammo',
    'item_spikes':           'item_rapidammo',
    'item_cells':            'item_rapidammo',
    'item_rockets':          'item_grenadeammo',
    'item_armor1':           'item_health',
    'item_armor2':           'item_health',
    'weapon_supershotgun':   'item_shotgun',
    'weapon_nailgun':        'item_rapid',
    'weapon_supernailgun':   'item_rapid',
    'weapon_lightning':      'item_rapid',
    'weapon_rocketlauncher': 'item_grenade',
    'weapon_grenadelauncher':'item_grenade',
}

# What the dropped deathmatch starts become. Order matters: the list is taken in
# file order, so re-running this script places the same furniture in the same
# places -- an importer whose output moved between runs would make every diff a
# whole-file diff.
#
# `wait` is seconds between releases, `maxalive` the ceiling this spawner holds
# at once; both are glasstower's numbers. `count 0` is "no limit", which is what
# an arena wants.
# THREE SPAWNERS, THREE THREATS, AND ONE OF THEM IS IN THE AIR.
#
# It used to be hound, caster, hound -- two of the same rusher and one ranged
# shooter, and nothing above the floor. tools/wavetest.c asks the SHIPPED arena
# "is at least one of them a flyer's" and then "is something off the ground",
# and both went red the day an imported map became that arena. The assertions
# were right and the furniture was wrong: a room with no flyer's spawner is a
# room where the flying path is authored, tested elsewhere, and never actually
# entered in play.
#
# WHICH SPAWNER CARRIES MON_FLIES HAS MOVED, and this list is one of the two
# places that had to be told. The `wraith` row left enemy.c and the flag came
# down onto the caster, so the arena's ranged spawner IS the air now -- it did
# not gain a fourth entry, it stopped being a floor spawner. The hound went the
# same way and the water spirit took its place, and the third slot is the brute,
# because the bestiary's one remaining rusher is the thing the list was short of
# either way.
#
# The boss's air wards already summon flyers, so before this the arena knew how
# to be three-dimensional during a boss fight and not for the fifteen waves
# before it -- which is the shape of an arena that teaches the player the wrong
# thing about its own ceiling.
#
# *스포너 셋, 위협 셋, 그리고 그중 하나는 공중에 있습니다.*
#
# 이전에는 하운드, 캐스터, 하운드였습니다. 같은 돌격형 둘과 원거리 하나이고, 바닥 위에는
# 아무것도 없었습니다. tools/wavetest.c가 *출하되는* 아레나에게 "그중 적어도 하나가 비행체의
# 것인가", 이어서 "무언가가 땅에서 떠 있는가"를 묻는데, 가져온 맵이 그 아레나가 된 날 둘 다
# 빨개졌습니다. 단언이 옳았고 가구가 틀렸습니다. 비행체의 스포너가 없는 방은 비행 경로가
# 저작되고 다른 곳에서 검사되며 정작 플레이에서는 결코 들어가지지 않는 방입니다.
#
# *어느 스포너가 MON_FLIES를 나르는지가 옮겨 갔고*, 이 목록이 그것을 알려야 했던 두 곳 중
# 하나입니다. `wraith` 행이 enemy.c를 떠나고 플래그가 캐스터로 내려왔으므로, 아레나의 원거리
# 스포너가 곧 공중입니다. 네 번째 항목이 늘어난 것이 아니라 지상 스포너이기를 그만둔 것입니다.
# 하운드도 같은 길을 갔고 그 자리를 물의 정령이 차지했으며, 세 번째 칸은 브루트입니다. 도감에
# 남은 유일한 돌격형이 어느 쪽이든 이 목록에 모자라던 것이기 때문입니다.
#
# 보스의 공중 결계핵은 이미 비행체를 소환하므로, 그 전까지 아레나는 보스전 동안에는 3차원일 줄
# 알면서 그 앞의 열다섯 웨이브 동안에는 아니었습니다.
# Spawners whose monsters fly, and how far above a floor-level start they go.
# The set is one name because MON_FLIES is one row in enemy.c's table; a second
# flyer would be a second line here and nothing else.
# 몬스터가 나는 스포너들과, 바닥 높이의 시작점보다 얼마나 위로 가는지입니다. 집합이 이름
# 하나인 이유는 enemy.c의 표에서 MON_FLIES가 한 행이기 때문입니다.
FLYER_SPAWNERS = {'monster_spawner_caster'}
FLYER_LIFT = 64        # two metres at BRUSH_UNIT 1/32

FURNITURE = [
    ('monster_spawner_water_spirit', {'wait': '6', 'count': '0', 'maxalive': '8'}),
    ('monster_spawner_caster', {'wait': '9', 'count': '0', 'maxalive': '6'}),
    ('monster_spawner_brute',  {'wait': '11', 'count': '0', 'maxalive': '4'}),
    ('info_altar',             {}),
]

# The boss, which the list above did not place and had no reason to until now.
#
# A WAVE ARENA NEEDS SPAWNERS; A BOSS ARENA ALSO NEEDS SOMETHING TO FIGHT.
# `enemy_boss_summon` raises the maw at the `monster_maw` marker the level
# carries, and a level with none raises nothing -- tools/bosstest.c asserts
# "it places a maw" against the SHIPPED arena precisely because that is
# invisible otherwise. While the boss lived in hand-authored glasstower the
# importer never needed to know about it.
#
# PLACED OPPOSITE THE ALTAR rather than at the next start in file order. The
# altar is where the reward lands and therefore where the player ends every
# wave; the maw is the thing they cross the room to reach. Putting them at the
# two ends of the same room is the difference between an arena and a corridor,
# and .map order is not a fact about the room -- world.c already refused that
# dependency by name: "'first in the entity list' is a property of how the map
# was saved."
#
# 위의 목록이 배치하지 않았고, 지금까지는 배치할 이유도 없었던 보스입니다.
#
# *웨이브 아레나에는 스포너가 필요하고, 보스 아레나에는 싸울 것도 필요합니다.*
# `enemy_boss_summon`은 레벨이 지닌 `monster_maw` 표식에서 아귀를 일으키며, 표식이 없는
# 레벨은 아무것도 일으키지 않습니다. tools/bosstest.c가 *출하되는* 아레나에 대해 "아귀를
# 배치한다"를 단언하는 이유가 바로 그것이 달리 보이지 않기 때문입니다.
#
# *파일 순서상 다음 시작점이 아니라 제단의 반대편에 놓습니다.* 제단은 보상이 떨어지는
# 자리이고 따라서 플레이어가 모든 웨이브를 끝내는 자리입니다. 아귀는 그들이 방을 가로질러
# 도달하는 것입니다. 둘을 같은 방의 양 끝에 두는 것이 아레나와 복도의 차이이며, .map 순서는
# 방에 대한 사실이 아닙니다.
MAW = 'monster_maw'


# --- ward candidates --------------------------------------------------------
#
# A WAVE ARENA IS NOT YET A BOSS ARENA. The furniture above makes an imported
# DM map an arena: it has spawners, so world.c's "a level that is an arena
# becomes one by having spawners" is satisfied. The BOSS is a second contract
# and the maps imported before this did not meet it. README.md said so plainly
# about lqdm11 and lqdm13 -- "neither carries info_ward_* markers, so a maw
# raised in one would have nothing to hide behind" -- and that was accurate
# while those maps were wave arenas nobody fought a boss in. It stops being
# acceptable the moment an imported map becomes WORLD_BOSS_ARENA.
#
# What the engine does with no candidates is documented and is not a crash:
# enemy_ward_place "raises nothing and returns 0, which leaves the maw open
# from its first frame ... a map that marked none would otherwise hold an
# unkillable boss in a level with no exit", and DIAG_WARD_CAND is raised. So
# the failure mode is a boss fight with its whole ward mechanic missing, which
# is exactly the kind of thing that plays as "this fight is oddly easy" rather
# than as a bug.
#
# AIR VERSUS GROUND IS ABOUT WHAT IT SUMMONS, not about where it stands.
# enemy.h is explicit: ward_table is "which summon table this ward draws from,
# 0 ground, 1 air", set "off the candidate marker's kind", and MON_WARD is one
# row rather than two because "the difference is off what walks out rather than
# off the ward". So this does not have to find floating positions -- which it
# could not verify anyway. It has to find SPREAD positions and decide which
# half summon flyers. Putting the flyer wards on the higher half is a choice
# about how the fight reads, not a requirement.
#
# *웨이브 아레나는 아직 보스 아레나가 아닙니다.* 위의 가구는 가져온 DM 맵을 아레나로 만듭니다.
# 스포너가 있으므로 world.c의 "아레나인 레벨은 이름이 아니라 스포너를 가짐으로써 아레나가
# 된다"가 충족됩니다. *보스*는 두 번째 계약이고, 이전에 가져온 맵들은 그것을 충족하지
# 않았습니다. README.md가 lqdm11과 lqdm13에 대해 분명히 말했습니다. "둘 다 info_ward_* 표식을
# 지니지 않으므로 그 안에서 일으킨 아귀는 뒤에 숨을 것이 없다." 그 맵들이 아무도 보스를 싸우지
# 않는 웨이브 아레나인 동안에는 정확한 말이었습니다. 가져온 맵이 WORLD_BOSS_ARENA가 되는
# 순간 그것은 받아들일 수 없게 됩니다.
#
# *공중형과 지상형의 차이는 무엇을 소환하는가이지 어디에 서는가가 아닙니다.* 그러므로 이
# 함수는 떠 있는 자리를 찾을 필요가 없습니다. 어차피 검증할 수도 없습니다. *퍼진* 자리를
# 찾고 어느 절반이 비행체를 부를지 정하면 됩니다.
WARD_KINDS = ('info_ward_ground', 'info_ward_air')


def origin_of(keys):
    """The `origin` triple as floats, or None if the entity has none."""
    v = keys.get('origin')
    if not v:
        return None
    parts = v.split()
    if len(parts) != 3:
        return None
    try:
        return tuple(float(p) for p in parts)
    except ValueError:
        return None


def _far2(a, b):
    """Squared XY distance, or -1 if either point is missing."""
    if not a or not b:
        return -1.0
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2


def spread(pts, n):
    """Up to `n` of `pts`, chosen far apart in XY. Farthest-point sampling.

    SPREAD IS THE WHOLE POINT. enemy_ward_place prefers candidates the previous
    cycle did not use, so that "somewhere new" means somewhere the player has
    to cross the room for. Two candidates a step apart satisfy the engine's
    not-the-same-index rule and satisfy nothing the rule was written for.

    Deterministic: the seed is the point farthest from the centroid, ties go to
    the earlier entity in .map order, and enemy_ward_scan sorts by position
    afterwards anyway. Nothing here may depend on a hash or a set's ordering --
    a map that converts differently on two machines is a map whose demos do not
    replay.

    *퍼짐이 요점의 전부입니다.* enemy_ward_place는 지난 사이클이 쓰지 않은 후보를 우선하며,
    그래야 "새로운 자리"가 플레이어가 방을 가로질러야 하는 자리를 뜻합니다. 한 걸음 떨어진
    후보 둘은 엔진의 "같은 인덱스가 아님" 규칙은 만족시키고 그 규칙이 쓰인 이유는 아무것도
    만족시키지 않습니다.
    """
    if not pts or n <= 0:
        return []
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    rest = list(pts)
    seed = max(rest, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2)
    rest.remove(seed)
    chosen = [seed]
    while len(chosen) < n and rest:
        far = max(rest, key=lambda p: min((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2
                                          for c in chosen))
        rest.remove(far)
        chosen.append(far)
    return chosen


def ward_slots(spare_starts, kept, cap, report):
    """Ward candidate markers at points the author verified a player can be at.

    THE SAME ARGUMENT THE SPAWNERS ARE PLACED BY. A deathmatch start and an
    item pickup are both points the map's author put something at and therefore
    checked: clear of geometry, on a surface, inside the level. Nothing else in
    a .map carries that guarantee, and picking ward positions by hand out of a
    thousand-brush arena means picking some of them inside a wall.

    STARTS BEFORE ITEMS, and only the starts the furniture did not take. A ward
    standing on a spawner is two things claiming one spot; a ward standing on a
    health pack is only slightly rude.

    HIGH HALF SUMMONS FLYERS. The pool is split at its own median height, so
    what the split means is "the upper part of this particular room" rather
    than a number that would be wrong in the next map.

    *스포너를 배치하는 것과 같은 논거입니다.* 데스매치 시작 지점과 아이템 획득 지점은 둘 다 맵
    제작자가 무언가를 놓았고 따라서 확인한 자리입니다. 지오메트리에서 떨어져 있고, 표면 위에
    있고, 레벨 안에 있습니다. .map의 다른 무엇도 그 보증을 지니지 않으며, 브러시 천 개짜리
    아레나에서 결계핵 자리를 손으로 고르는 것은 그중 일부를 벽 안에 고르는 일입니다.
    """
    pool = []
    for k in spare_starts:
        p = origin_of(k)
        if p:
            pool.append(p)
    n_from_starts = len(pool)
    for k, _ in kept:
        if k.get('classname', '').startswith('item_'):
            p = origin_of(k)
            if p:
                pool.append(p)

    if not pool:
        report['wards'] = (0, 0, 0)
        return []

    # Two lists share the cap, so each gets half of it.
    # 두 목록이 상한을 나누어 쓰므로 각각 절반을 받습니다.
    per = max(1, cap // 2)

    by_z = sorted(pool, key=lambda p: p[2])
    cut = len(by_z) // 2
    low, high = by_z[:cut] or by_z, by_z[cut:] or by_z

    ground = spread(low, per)
    air = spread(high, per)

    out = []
    for kind, pts in ((WARD_KINDS[0], ground), (WARD_KINDS[1], air)):
        for p in pts:
            out.append(({'classname': kind,
                         'origin': '%g %g %g' % p}, []))

    report['wards'] = (len(ground), len(air), n_from_starts)
    return out


# --- the .map itself --------------------------------------------------------

def parse(text):
    """Top-level entities, each as (keys dict, [brush text]).

    A .map is braces two deep and nothing else: an entity is a block, and a
    brush is a block inside one. Faces are kept as TEXT rather than parsed into
    planes -- this script rewrites one token per face and must not become a
    second opinion about what a plane means. brush.c is the only parser here.
    """
    ents, i, n = [], 0, len(text)
    while i < n:
        if text[i] != '{':
            i += 1
            continue
        i += 1
        keys, brushes, depth = {}, [], 0
        buf = []
        while i < n:
            c = text[i]
            if c == '{':
                depth += 1
                if depth == 1:
                    buf = []
                    i += 1
                    continue
            elif c == '}':
                if depth == 0:
                    i += 1
                    break
                depth -= 1
                if depth == 0:
                    brushes.append(''.join(buf))
                    i += 1
                    continue
            if depth == 0:
                m = re.match(r'\s*"([^"]*)"\s+"([^"]*)"', text[i:])
                if m:
                    keys[m.group(1)] = m.group(2)
                    i += m.end()
                    continue
            else:
                buf.append(c)
            i += 1
        ents.append((keys, brushes))
    return ents


FACE = re.compile(r'^(\s*\(.*\)\s*\(.*\)\s*\(.*\)\s+)(\S+)(.*)$')


# Faces that mark a brush as bounding space without drawing any of it. A brush
# made ENTIRELY of these is invisible and solid -- and this game has a grapple.
# 면이 공간을 한정하되 아무것도 그리지 않음을 표시하는 텍스처입니다. 이것만으로 이루어진
# 브러시는 보이지 않으면서 단단하며, 이 게임에는 갈고리가 있습니다.
INVISIBLE_SOLID = {'clip', 'skip'}


def solid_only(brushes, report):
    """Drop the brushes that are invisible walls, keep everything else.

    A CLIP BRUSH IS A HOOK TARGET YOU CANNOT SEE. Quake mappers use `clip` to
    smooth a staircase, round off a doorframe or fence a player away from a
    ledge: solid, and drawn as nothing. brush.c honours both halves -- it skips
    the faces in ::brush_geometry and leaves ::Brush::solid set -- so an
    imported map arrives with walls that stop you and are not there.

    That is a fair trade in the game those maps were built for, where the only
    thing you do to a wall is bump into it. This game's hook attaches to
    geometry, so an invisible wall is a grapple point hanging in mid-air, and
    `lqdm1` has seventy of them.

    WHOLE BRUSHES ONLY, and the count says the distinction is not academic: of
    807 brushes, 70 are entirely `clip` and ZERO mix clip faces with drawn ones.
    A brush with one nodraw face among five drawn ones is a wall with a hidden
    back, and deleting it would delete the wall.

    `trigger` is deliberately NOT on the list. A trigger volume is also
    invisible and also bounds space, but the engine READS it -- level.c turns it
    into a trigger -- so dropping it would delete behaviour rather than an
    obstacle.

    *클립 브러시는 볼 수 없는 갈고리 표적입니다.* Quake 제작자들은 `clip`으로 계단을
    매끄럽게 하고, 문틀 모서리를 둥글리고, 플레이어를 난간에서 떼어 놓습니다. 단단하고,
    아무것도 그려지지 않습니다. brush.c는 양쪽 모두를 지킵니다. ::brush_geometry에서 그 면들을
    건너뛰고 ::Brush::solid는 설정된 채로 둡니다. 그래서 가져온 맵은 당신을 막으면서 그곳에
    없는 벽들과 함께 도착합니다.
    그 맵들이 만들어진 게임에서는 공정한 거래입니다. 벽에 하는 일이 부딪히는 것뿐이니까요.
    이 게임의 갈고리는 지오메트리에 붙으므로, 보이지 않는 벽은 허공에 걸린 갈고리 지점이며
    `lqdm1`에는 그것이 일흔 개 있습니다.

    *브러시 전체만이며*, 그 수가 이 구별이 탁상공론이 아님을 말합니다. 브러시 807개 중 70개가
    온전히 `clip`이고, 클립 면과 그려지는 면을 섞은 것은 *0개*입니다. 그려지는 면 다섯 중
    nodraw 면 하나를 가진 브러시는 뒷면이 가려진 벽이며, 그것을 지우는 것은 벽을 지우는
    일입니다.

    `trigger`는 일부러 목록에 없습니다. 트리거 부피도 보이지 않고 공간을 한정하지만 엔진이
    그것을 *읽습니다*. 지우면 장애물이 아니라 동작을 지우게 됩니다.
    """
    out = []
    for b in brushes:
        names = set()
        for line in b.splitlines():
            m = FACE.match(line)
            if m:
                names.add(m.group(2).lower())
        if names and names <= INVISIBLE_SOLID:
            for n in sorted(names):
                report.setdefault('clipped', {}).setdefault(n, 0)
                report['clipped'][n] += 1
            continue
        out.append(b)
    return out


def retexture(brush, report):
    """Face texture names, kept rather than substituted.

    THIS USED TO REPLACE THEM. Every face was looked up in TEXTURES and given
    one of this project's own materials instead -- `med_csl_brk14b` became
    `wall_stone`, four `met_brn_*` became two of ours, four woods became one
    `pwood`. Twenty-three names became eleven, and the result was LibreQuake's
    room wearing somebody else's walls: the geometry crossed and the surface did
    not.

    assets/sprites/import-librequake-textures.py brings the surface across too,
    so there is nothing left to substitute. What remains here is the two things
    a name still cannot be:

      * NOT A FILENAME. A face's material name IS its file name, and Quake
        marks a liquid with a leading `*`, which Windows will not accept in
        one. LibreQuake's own repository spells those files `star_*`, and the
        texture importer writes them under that name, so this renames the faces
        to match. The two ends have to say the same word.
      * TOO LONG FOR ::BR_TEX. brush.c truncates a name past its buffer and
        raises DIAG_BRUSH_CAP, which is how `spire` ended up with two different
        walls resolving to one material for its whole life. Reported here
        instead, while it is still a conversion and not a mystery.

    TEXTURES survives for the dev-wad greyboxes, which name surfaces that have
    no art to import -- `wall_grey_c` is a development texture, not a wall
    somebody drew -- and for the teleport surfaces this engine has no idea what
    to do with.

    *예전에는 이것들을 치환했습니다.* 모든 면을 TEXTURES에서 찾아 이 프로젝트 자신의 재질을
    대신 주었습니다. 이름 스물셋이 열하나가 되었고, 그 결과는 남의 벽을 입은 LibreQuake의
    방이었습니다. 지오메트리는 건너왔고 표면은 건너오지 않았습니다.

    assets/sprites/import-librequake-textures.py가 표면도 함께 가져오므로 치환할 것이 남아
    있지 않습니다. 이곳에 남는 것은 이름이 여전히 될 수 없는 두 가지입니다. *파일명이 아닌 것*과
    ::BR_TEX보다 *긴 것*입니다.
    """
    out = []
    for line in brush.splitlines():
        m = FACE.match(line)
        if not m:
            if line.strip():
                out.append(line)
            continue
        name = m.group(2)
        if name in TEXTURES:
            name = TEXTURES[name]
        elif name.startswith('*'):
            name = 'star_' + name[1:]
            report.setdefault('renamed', {}).setdefault(m.group(2), name)
        if len(name) > CAPS['tex name'][1]:
            report.setdefault('too_long', {}).setdefault(name, len(name))
        report.setdefault('kept_textures', set()).add(name)
        out.append(m.group(1) + name + m.group(3))
    return '\n'.join(out)


def strip_editor_keys(keys, report):
    """Drop TrenchBroom's own bookkeeping, which this engine cannot store.

    `_tb_*` keys record the EDITOR's state, not the level's: which layer a
    brush is on, which linked-group instance it belongs to, and the 4x4 matrix
    that places that instance. A compiler consumes and discards them. This
    engine has no compiler, so they arrive at brush.c's parser -- where
    `_tb_transformation` is 98 to 136 characters against a BR_VAL of 64, gets
    truncated, and raises DIAG_MAPENT_CAP.

    FOUND BY THE COUNTER, WHICH IS WHY THE COUNTER EXISTS. lqdm2 and lqdm4 both
    reported `mapent=2` in tools/mapcap.c while sitting at 104 and 91 entities
    against a cap of 192 -- the refusal was not the entity cap it is named
    after, and nothing but the count said anything was wrong at all. The two
    maps already shipped carry no linked groups, so this was invisible until a
    map that used them was measured.

    Dropped rather than shortened: a truncated matrix is not a shorter matrix,
    and there is nothing here that reads it.

    TrenchBroom 자신의 부기 정보를 버립니다. 이 엔진은 그것을 저장할 수 없습니다.

    `_tb_*` 키는 레벨이 아니라 *에디터*의 상태를 기록합니다. 브러시가 어느 레이어에 있는지,
    어느 연결 그룹 인스턴스에 속하는지, 그리고 그 인스턴스를 배치하는 4x4 행렬입니다.
    컴파일러가 그것을 소비하고 버립니다. 이 엔진에는 컴파일러가 없으므로 그것들은 brush.c의
    파서에 도달하며, 그곳에서 `_tb_transformation`은 BR_VAL 64에 대해 98~136자이고, 잘리며,
    DIAG_MAPENT_CAP을 올립니다.

    *카운터가 찾았고, 그것이 카운터가 존재하는 이유입니다.* lqdm2와 lqdm4는 상한 192에 대해
    엔티티 104개와 91개로 앉아 있으면서 tools/mapcap.c에서 둘 다 `mapent=2`를 보고했습니다.
    그 거절은 이름이 가리키는 엔티티 상한이 아니었고, 그 수 말고는 무엇도 잘못되었다고 말하지
    않았습니다. 이미 출하된 두 맵에는 연결 그룹이 없어서, 그것을 쓴 맵을 재기 전까지 이것은
    보이지 않았습니다.

    줄이지 않고 버립니다. 잘린 행렬은 짧은 행렬이 아니며, 이곳에는 그것을 읽는 것이 없습니다.
    """
    out = {}
    for k, v in keys.items():
        if k.startswith('_tb_'):
            report.setdefault('editor_keys', {}).setdefault(k, 0)
            report['editor_keys'][k] += 1
            continue
        out[k] = v
    return out


def emit(keys, brushes):
    body = ['{']
    for k, v in keys.items():
        body.append('"%s" "%s"' % (k, v))
    for b in brushes:
        body.append('{')
        body.append(b.strip('\n'))
        body.append('}')
    body.append('}')
    return '\n'.join(body)


def convert(text, report):
    ents = parse(text)

    world = None
    kept, dm_starts = [], []

    for keys, brushes in ents:
        # Before anything looks at the classname, because a merged or dropped
        # entity's keys never reach emit() and its editor bookkeeping would
        # never be counted -- and the count is how anyone learns the map had
        # any.
        # 무엇이 classname을 보기 전에 합니다. 병합되거나 버려지는 엔티티의 키는 emit()에
        # 도달하지 않으므로 그 에디터 부기 정보가 결코 세어지지 않을 것이고, 맵에 그것이
        # 있었다는 사실을 누군가 알게 되는 경로가 바로 그 수이기 때문입니다.
        keys = strip_editor_keys(keys, report)
        cn = keys.get('classname', '')

        if cn == 'worldspawn':
            # `wad` names the author's texture files and would send an editor
            # looking for WADs this tree does not ship. `message` and `_credits`
            # STAY: the attribution travels with the data, which is the one
            # place it cannot be separated from what it attributes.
            keys = {k: v for k, v in keys.items() if k != 'wad'}
            world = (keys, [retexture(b, report)
                            for b in solid_only(brushes, report)])
            continue

        if cn in MERGE_INTO_WORLD:
            report['merged'] = report.get('merged', 0) + 1
            world[1].extend(retexture(b, report)
                            for b in solid_only(brushes, report))
            continue

        if cn in DROP:
            report.setdefault('dropped', {}).setdefault(cn, 0)
            report['dropped'][cn] += 1
            continue

        if cn == 'info_player_deathmatch':
            dm_starts.append(keys)
            continue

        if cn in FROZEN_MOVERS:
            report.setdefault('frozen', {}).setdefault(cn, 0)
            report['frozen'][cn] += 1

        if cn in ITEMS:
            keys = dict(keys)
            keys['classname'] = ITEMS[cn]
            report.setdefault('remapped', {}).setdefault(cn, 0)
            report['remapped'][cn] += 1

        if cn in LIGHTS:
            keys = dict(keys)
            keys['classname'] = 'light'
            # The author's own keys win: a flame that was given an explicit
            # `light` or `_color` in the .map is stating something the name
            # cannot, and the name should not overwrite it.
            # 제작자 자신의 키가 이깁니다. .map에서 명시적으로 `light`나 `_color`를 받은
            # 불꽃은 이름이 말할 수 없는 것을 말하고 있으며, 이름이 그것을 덮어써서는
            # 안 됩니다.
            for k, v in LIGHTS[cn].items():
                keys.setdefault(k, v)
            report.setdefault('remapped', {}).setdefault(cn, 0)
            report['remapped'][cn] += 1

        # THE SILENT ARM, MADE LOUD. Everything that reached here without
        # matching a table is about to cross as itself, so this is the last
        # place the conversion can notice that the engine will throw it away.
        # *조용한 갈래를 시끄럽게 만듭니다.* 어느 표에도 걸리지 않고 이곳에 닿은 모든 것이
        # 곧 자기 자신으로 건너갑니다. 그러므로 이곳이 엔진이 그것을 버릴 것임을 변환이
        # 알아챌 수 있는 마지막 자리입니다.
        if not engine_understands(keys.get('classname', '')):
            cn2 = keys.get('classname', '(no classname)')
            report.setdefault('unknown', {}).setdefault(cn2, 0)
            report['unknown'][cn2] += 1

        kept.append((keys, [retexture(b, report)
                            for b in solid_only(brushes, report)]))

    # The furniture, at points the author verified a player can stand on.
    placed = []
    for i, (cn, extra) in enumerate(FURNITURE):
        if i >= len(dm_starts):
            report.setdefault('unplaced', []).append(cn)
            continue
        keys = {'classname': cn, 'origin': dm_starts[i]['origin']}

        # A FLYER'S SPAWNER NEEDS AIR UNDER IT, and a deathmatch start has
        # none: it is by construction a place a player's feet go.
        # enemy.c is explicit that "a flyer keeps the height it was spawned at
        # and never asks the floor about it", so a caster made at a start hovers
        # at the floor for its whole life -- a flying monster that never flies.
        # tools/wavetest.c measured exactly that on the shipped arena: with the
        # spawner placed, "at least one of them is a flyer's" passed and "and
        # something is off the ground" still failed, which is the difference
        # between having the entity and having the behaviour.
        #
        # FLYER_LIFT is two metres in this engine's units (::BRUSH_UNIT is
        # 1/32, so 64 units), which clears a player's head and sits under any
        # ceiling a Quake deathmatch start has above it -- a start must already
        # have standing room and jump clearance, or the author could not have
        # spawned there.
        #
        # *비행체의 스포너에는 아래에 공기가 필요하고*, 데스매치 시작점에는 그것이 없습니다.
        # 구조적으로 플레이어의 발이 놓이는 자리이기 때문입니다. enemy.c는 "비행체는 생성된
        # 높이를 유지하며 바닥에 그것을 묻지 않는다"고 분명히 말하므로, 시작점에서 만들어진
        # 캐스터는 평생 바닥 높이에 떠 있습니다. 결코 날지 않는 비행 몬스터입니다.
        # tools/wavetest.c가 출하되는 아레나에서 정확히 그것을 쟀습니다. 스포너를 놓자 "그중
        # 적어도 하나가 비행체의 것"은 통과했고 "무언가가 땅에서 떠 있다"는 여전히
        # 실패했습니다. 엔티티를 가진 것과 동작을 가진 것의 차이입니다.
        if cn in FLYER_SPAWNERS:
            o = origin_of(keys)
            if o:
                keys['origin'] = '%g %g %g' % (o[0], o[1], o[2] + FLYER_LIFT)
                report['flyer_lift'] = FLYER_LIFT

        keys.update(extra)
        placed.append((keys, []))
    # The maw, at the spare start farthest from the altar. Falls back to the
    # unused starts only -- a maw standing on a spawner would be raised inside
    # the thing that feeds the wave it interrupts.
    # 아귀를, 남은 시작점 중 제단에서 가장 먼 곳에 둡니다. 쓰이지 않은 시작점만을 후보로
    # 삼습니다. 스포너 위에 선 아귀는 자신이 끊는 웨이브를 먹여 살리는 것 안에서 일어나게
    # 됩니다.
    spare = dm_starts[len(FURNITURE):]
    altar = None
    for keys, _ in placed:
        if keys.get('classname') == 'info_altar':
            altar = origin_of(keys)
    maw_at = None
    if spare and altar:
        best = max(spare, key=lambda k: _far2(origin_of(k), altar))
        maw_at = origin_of(best)
        spare = [k for k in spare if k is not best]
    elif spare:
        maw_at = origin_of(spare[0])
        spare = spare[1:]

    if maw_at:
        placed.append(({'classname': MAW, 'origin': '%g %g %g' % maw_at}, []))
    else:
        report.setdefault('unplaced', []).append(MAW)

    report['dm_starts'] = len(dm_starts)
    report['placed'] = len(placed)

    wards = ward_slots(spare, kept,
                       CAPS['ward cands'][1], report)

    out = [world] + kept + placed + wards

    # A NAME NOTHING CAN CALL IS A DOOR THAT NEVER OPENS.
    #
    # level.c reads a func_door's `targetname` into DoorDef::tag, and door.c
    # then branches on it: "An untagged door opens to a touch on itself; a
    # tagged one opens only" when something fires its tag --
    #     if (d->tag > 0) asked = tagged;
    #     else            asked = dist_to_outline(...) <= DOOR_TOUCH_DIST;
    # So a name is a PROMISE that something will call it, and the crossing
    # breaks that promise on purpose: trigger_once and trigger_relay are in DROP,
    # because neither has a counterpart here. (trigger_teleport was in that list
    # and is not any more -- it has one now.) lqdm1's two gates were named
    # `gate1` and the only thing that fired
    # them was a trigger_once. Both arrived tagged, waiting on a switch this
    # converter had already deleted, and could not be opened by any means the
    # game has.
    #
    # THE UNTAGGED BEHAVIOUR IS THE ONE THAT WAS WANTED ANYWAY: door.c opens on
    # proximity within DOOR_TOUCH_DIST and re-arms DOOR_OPEN_TIME every frame
    # the player is near, so walking away lets it close on its own. Dropping a
    # dead name does not invent behaviour; it stops suppressing the default.
    #
    # DEAD, NOT MERELY PRESENT. The test is whether anything that SURVIVED
    # targets the name -- so a door still driven by a switch that crossed keeps
    # its tag and keeps needing the switch. Only the orphans are freed.
    #
    # *아무도 부를 수 없는 이름은 결코 열리지 않는 문입니다.*
    #
    # level.c는 func_door의 `targetname`을 DoorDef::tag로 읽고, door.c가 그것으로 갈라집니다.
    # 태그 없는 문은 자기 자신에 대한 접촉으로 열리고, 태그 있는 문은 무언가가 그 태그를 쏠
    # 때에만 열립니다. 그러므로 이름은 무언가가 그것을 부르리라는 *약속*이며, 이 변환은 그
    # 약속을 의도적으로 깨뜨립니다. trigger_once, trigger_relay, trigger_teleport이 전부
    # DROP에 있기 때문입니다. lqdm1의 두 문은 `gate1`이라는 이름을 달고 있고 그것을 쏘던 것은
    # trigger_once 하나뿐이었습니다. 둘 다 이 변환기가 이미 지운 스위치를 기다리는 태그를 단
    # 채 도착했고, 게임이 가진 어떤 수단으로도 열 수 없었습니다.
    #
    # *태그 없는 동작이 애초에 원하던 그 동작입니다.* door.c는 DOOR_TOUCH_DIST 안에서 근접으로
    # 열고 플레이어가 가까이 있는 매 프레임 DOOR_OPEN_TIME을 다시 채우므로, 걸어 나가면 스스로
    # 닫힙니다. 죽은 이름을 버리는 것은 동작을 지어내는 것이 아니라 기본 동작을 억누르기를
    # 그만두는 것입니다.
    #
    # *단지 있는 것이 아니라 죽은 것만입니다.* 판단 기준은 *살아남은* 무엇이 그 이름을
    # 겨누는가이므로, 건너온 스위치가 여전히 구동하는 문은 태그를 유지하고 스위치를 계속
    # 필요로 합니다. 고아만 풀려납니다.
    reachable = set()
    for keys, _ in out:
        t = keys.get('target')
        if t:
            reachable.add(t)
    for keys, _ in out:
        name = keys.get('targetname')
        if name and name not in reachable:
            del keys['targetname']
            report.setdefault('orphaned', {}).setdefault(
                '%s "%s"' % (keys.get('classname', '?'), name), 0)
            report['orphaned']['%s "%s"' % (keys.get('classname', '?'), name)] += 1

    report['entities'] = len(out)
    report['brushes'] = sum(len(b) for _, b in out)

    # FACES PER BRUSH, the cap this script named and never counted.
    #
    # BR_MAX_FACES has been in CAP_NAMES since the table was written and
    # nothing ever compared anything to it, so the one map in the pack that
    # exceeds it did so silently: lqdm3 carries two brushes of 33 faces
    # against a cap of 32, and brush.c drops the surplus face. A brush missing
    # one of its faces is not a smaller brush -- it is an open box, and the
    # room behind it is visible through the gap.
    #
    # Counted from the face TEXT rather than from parsed planes, for the same
    # reason parse() keeps faces as text: brush.c is the only parser here and
    # this must not become a second opinion. A face is a line beginning with
    # '(' inside a brush block, which is what the format is.
    #
    # *브러시당 면의 수*, 이 스크립트가 이름 붙여 놓고 한 번도 세지 않은 상한입니다.
    #
    # BR_MAX_FACES는 표가 쓰인 이래로 CAP_NAMES에 있었지만 무엇도 그것과 비교되지 않았고,
    # 그래서 팩에서 그것을 넘는 단 하나의 맵이 조용히 넘겼습니다. lqdm3은 상한 32에 대해 면
    # 33개짜리 브러시 둘을 지니며, brush.c는 초과된 면을 버립니다. 면 하나가 빠진 브러시는 더
    # 작은 브러시가 아닙니다. *열린 상자*이고, 그 뒤의 방이 그 틈으로 보입니다.
    worst, over = 0, 0
    for _, brushes in out:
        for b in brushes:
            n = sum(1 for line in b.split('\n') if line.strip().startswith('('))
            worst = max(worst, n)
            if n > CAPS['faces/brush'][1]:
                over += 1
    report['faces_per_brush'] = (worst, over)

    # THE CAPS THAT ARE NOT BRUSHES OR ENTITIES, counted because they are the
    # ones that overflow QUIETLY. Past BR_MAX_ENTS a level fails to load and
    # says so; past LVL_MAX_HAZARDS the level loads perfectly and one pool of
    # lava simply does not hurt, which is indistinguishable from a pool that
    # was authored that way. lqdm13 arrives with exactly 8 of 8 -- no headroom
    # at all -- so a map with one more would lose one without a word.
    counts = {}
    for keys, _ in out:
        cn = keys.get('classname', '')
        if cn == 'light':                counts['lights'] = counts.get('lights', 0) + 1
        elif cn == 'trigger_hurt':       counts['hazards'] = counts.get('hazards', 0) + 1
        # BEFORE the generic `trigger_` branch, because a teleporter is not a
        # trigger to the engine: level.c stores it in Level::teleports against
        # LVL_MAX_TELEPORTS (8), not in Level::triggers against LVL_MAX_TRIGGERS
        # (16). Counting it as a trigger overstated one cap by two and left the
        # other unwatched -- and this table exists so that a map is refused here
        # rather than loading with half of itself missing.
        # 일반 `trigger_` 분기보다 *먼저*입니다. 엔진에게 텔레포터는 트리거가 아니기
        # 때문입니다. level.c는 그것을 LVL_MAX_TRIGGERS(16)에 대한 Level::triggers가 아니라
        # LVL_MAX_TELEPORTS(8)에 대한 Level::teleports에 저장합니다. 트리거로 세면 한쪽
        # 상한을 둘만큼 부풀리고 다른 쪽은 지켜보지 않게 됩니다. 이 표가 존재하는 이유는 맵이
        # 자기 절반을 잃은 채 로드되는 대신 이곳에서 거절되게 하려는 것입니다.
        elif cn == 'trigger_teleport': counts['teleports'] = counts.get('teleports', 0) + 1
        elif cn.startswith('trigger_'):  counts['triggers'] = counts.get('triggers', 0) + 1
        elif cn == 'func_door':          counts['doors'] = counts.get('doors', 0) + 1
        elif cn.startswith(('item_', 'monster_')) or cn in ('info_altar',):
            counts['level ents'] = counts.get('level ents', 0) + 1
        elif cn in WARD_KINDS:
            # A marker is a level entity like any other -- level.c maps
            # info_ward_* to "wardground"/"wardair" and they occupy
            # LVL_MAX_ENTS slots. Sixteen of them is a quarter of that budget,
            # which is a real cost and is why it is counted here rather than
            # discovered as a level that quietly lost its last few items.
            # 표식도 다른 것과 같은 레벨 엔티티입니다. level.c가 info_ward_*를
            # "wardground"/"wardair"로 대응시키며 LVL_MAX_ENTS 슬롯을 차지합니다. 열여섯
            # 개는 그 예산의 4분의 1이고, 그것은 실제 비용이며, 그래서 마지막 아이템 몇 개를
            # 조용히 잃은 레벨로 발견되지 않고 이곳에서 세어집니다.
            counts['level ents'] = counts.get('level ents', 0) + 1
            counts['ward cands'] = counts.get('ward cands', 0) + 1
    report['counts'] = counts
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    name = sys.argv[1]

    if '--from' in sys.argv:
        where = sys.argv[sys.argv.index('--from') + 1]
        text = open(where, encoding='utf-8', errors='replace').read()
    else:
        where = SRC % name
        text = urllib.request.urlopen(where).read().decode('utf-8', 'replace')

    # A converted map has this script's own header on it. Converting one again
    # would strip the spawners it placed the first time -- see the note at the
    # top -- so it is refused rather than performed.
    if 'import-librequake.py' in text[:1024]:
        print('%s has already been converted. Re-run against the LibreQuake '
              'source, not against the output.' % where)
        return 1

    report = {}
    ents = convert(text, report)

    # `Game: SFPS`, NOT `Game: Quake`, AND THE LINE IS NOT A COMMENT.
    #
    # TrenchBroom reads these two lines to decide which game configuration to
    # open a .map with, and the conversion is not finished until it says this
    # one. A map that arrives saying `Quake` opens against Quake's config: the
    # material browser looks for Quake WADs instead of assets\sprites\, and
    # SFPS.fgd is not loaded, so every entity this converter placed --
    # monster_spawner_*, info_altar, info_ward_air, info_ward_ground,
    # monster_maw -- is an unknown classname the editor draws as a grey box and
    # cannot give a property sheet to. The map would load and be uneditable in
    # the only editor this project has.
    #
    # It stopped being a Quake map at the top of this file. Saying so here is
    # what makes it one of ours.
    #
    # *`Game: Quake`가 아니라 `Game: SFPS`이며, 이 줄은 주석이 아닙니다.*
    #
    # TrenchBroom은 이 두 줄을 읽고 어느 게임 설정으로 .map을 열지 정하며, 이 줄이 이것을
    # 말하기 전까지 변환은 끝난 것이 아닙니다. `Quake`라고 말하며 도착한 맵은 Quake의 설정으로
    # 열립니다. 재질 브라우저는 assets\sprites\ 대신 Quake WAD를 찾고, SFPS.fgd는 로드되지
    # 않으므로, 이 변환기가 배치한 모든 엔티티(monster_spawner_*, info_altar, info_ward_air,
    # info_ward_ground, monster_maw)가 에디터가 회색 상자로 그리고 속성 시트를 줄 수 없는 알 수
    # 없는 classname이 됩니다. 그 맵은 로드는 되지만 이 프로젝트가 가진 유일한 에디터에서
    # 편집할 수 없습니다.
    #
    # 이것은 이 파일 맨 위에서 이미 Quake 맵이기를 그만두었습니다. 이곳에서 그렇게 말하는 것이
    # 그것을 우리 것으로 만듭니다.
    header = [
        '// Game: SFPS',
        '// Format: Valve',
        '//',
        '// %s, from LibreQuake, under the 3-clause BSD licence.' % name,
        '// Converted by assets/maps/import-librequake.py -- see that file for',
        '// what the crossing costs and why this map gained spawners it did not',
        '// ship with. The licence text is in docs/LICENSE-LibreQuake.txt.',
    ]
    body = '\n'.join(header) + '\n' + '\n'.join(emit(k, b) for k, b in ents) + '\n'

    print('read %s' % where)
    print()
    print('  entities   %4d  (%s %d)' % (report['entities'], *CAPS['entities']))
    print('  brushes    %4d  (%s %d)' % (report['brushes'], *CAPS['brushes']))
    print('  merged into worldspawn: %d brush containers' % report.get('merged', 0))
    worst, over = report.get('faces_per_brush', (0, 0))
    if over:
        print('  faces/brush   %4d  (%s %d)  <-- %d BRUSH(ES) OVER'
              % (worst, *CAPS['faces/brush'], over))
        print('    ^ brush.c drops the surplus face. A brush missing a face is')
        print('      an open box, not a smaller one.')
    else:
        print('  faces/brush   %4d  (%s %d)' % (worst, *CAPS['faces/brush']))
    print('  deathmatch starts: %d, of which %d became arena furniture'
          % (report['dm_starts'], report['placed']))
    g, a, from_starts = report.get('wards', (0, 0, 0))
    if g or a:
        print('  ward candidates: %d ground + %d air, %d of them on the '
              'starts the furniture left' % (g, a, from_starts))
        if g + a < CAPS['ward cands'][1]:
            print('    ^ under BOSS_MAX_CAND (%d). enemy_ward_place wants twice'
                  % CAPS['ward cands'][1])
            print('      BOSS_WARDS per list to place a fresh set each cycle;'
                  ' fewer means repeats.')
    else:
        print('  NO WARD CANDIDATES -- a boss raised here would have no wards')
        print('  and its maw would be open from the first frame'
              ' (DIAG_WARD_CAND).')
    print()
    if report.get('remapped'):
        print('  remapped:')
        for k, v in sorted(report['remapped'].items()):
            # Two tables feed this line now. Looking only in ITEMS raised a
            # KeyError the moment LIGHTS gained its first entry -- a report
            # that crashes on the thing it is reporting.
            # 이제 두 표가 이 줄에 들어옵니다. ITEMS만 뒤지면 LIGHTS가 첫 항목을 얻는 순간
            # KeyError가 났습니다. 보고하려는 바로 그것에 대해 죽는 보고입니다.
            to = ITEMS.get(k) or ('light' if k in LIGHTS else '?')
            print('    %-26s -> %-20s x%d' % (k, to, v))
    # LOUDER THAN THE REST OF THE REPORT, because every other line here
    # describes a decision somebody made and this one describes a hole.
    # A name printed here reaches the .map and is deleted at load: the
    # entity is in the file, absent from the game, and nothing at either
    # end says so. Put it in DROP if it should not cross, or give it a
    # `monster_`/`item_` name in ITEMS if it should.
    # *보고서의 나머지보다 시끄럽습니다.* 이곳의 다른 모든 줄은 누군가 내린 결정을
    # 서술하고, 이 줄은 구멍을 서술하기 때문입니다. 이곳에 찍힌 이름은 .map에 닿았다가
    # 로드 시점에 삭제됩니다. 엔티티는 파일에 있고 게임에는 없으며, 양쪽 끝 어디에서도
    # 그렇다고 말하지 않습니다. 건너오지 말아야 한다면 DROP에, 건너와야 한다면 ITEMS에서
    # `monster_`/`item_` 이름을 주십시오.
    if report.get('unknown'):
        print('  !! THE ENGINE HAS NO NAME FOR THESE. They are written to the'
              ' .map and dropped at load:')
        for k, v in sorted(report['unknown'].items()):
            print('    %-30s x%d' % (k, v))

    if report.get('dropped'):
        print('  dropped:')
        for k, v in sorted(report['dropped'].items()):
            print('    %-26s x%-3d %s' % (k, v, DROP[k]))
    if report.get('frozen'):
        print('  ARRIVE BUT DO NOT MOVE -- their brushes are solid where the')
        print('  author left them, and a lift that does not lift is a route')
        print('  the player can see and cannot take:')
        for k, v in sorted(report['frozen'].items()):
            print('    %-26s x%d' % (k, v))

    if report.get('orphaned'):
        print('  names freed (nothing that survived could fire them, so the')
        print('  door waited on a switch this conversion had deleted):')
        for k, v in sorted(report['orphaned'].items()):
            print('    %-30s x%d  -> opens on approach' % (k, v))

    if report.get('clipped'):
        print('  invisible solid brushes dropped (a hook attaches to geometry,')
        print('  and a clip brush is geometry you cannot see):')
        for k, v in sorted(report['clipped'].items()):
            print('    all-%-22s x%d' % (k, v))

    if report.get('editor_keys'):
        print('  editor bookkeeping dropped (TrenchBroom state, not level data;')
        print('  _tb_transformation alone is longer than BR_VAL and would be')
        print('  truncated into a DIAG_MAPENT_CAP nobody could attribute):')
        for k, v in sorted(report['editor_keys'].items()):
            print('    %-26s x%d' % (k, v))

    kept = report.get('kept_textures', set())
    if kept & NODRAW:
        print('  crossed unchanged (brush.c draws none of these):')
        print('    %s' % ', '.join(sorted(kept & NODRAW)))
    if kept - NODRAW:
        print('  UNKNOWN TEXTURES -- add them to TEXTURES or they ship as')
        print('  whatever textures.txt falls back to:')
        for t in sorted(kept - NODRAW):
            print('    %s' % t)
    if report.get('unplaced'):
        print('  NOT PLACED (too few deathmatch starts): %s'
              % ', '.join(report['unplaced']))

    print()
    print('  against the caps that overflow quietly:')
    for key in ('level ents', 'lights', 'doors', 'triggers', 'teleports',
                'hazards', 'ward cands'):
        cname, cap = CAPS[key]
        got = report['counts'].get(key, 0)
        flag = ' <-- FULL' if got == cap else (' <-- OVER' if got > cap else '')
        print('    %-11s %3d / %-3d  %s%s' % (key, got, cap, cname, flag))

    over = []
    if report['entities'] > CAPS['entities'][1]:
        over.append('entities')
    if report['brushes'] > CAPS['brushes'][1]:
        over.append('brushes')
    for key, (_, cap) in CAPS.items():
        if report['counts'].get(key, 0) > cap:
            over.append(key)
    if over:
        print()
        print('  OVER CAPACITY: %s. The level would load with part of it '
              'missing.' % ', '.join(over))

    if '--emit' in sys.argv:
        with open(name + '.map', 'w', encoding='utf-8', newline='\n') as f:
            f.write(body)
        print()
        print('wrote %s.map  (%d bytes)' % (name, len(body)))
    else:
        print()
        print('(nothing written -- pass --emit)')
    return 1 if over else 0


if __name__ == '__main__':
    sys.exit(main())
