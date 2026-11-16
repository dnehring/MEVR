# SPDX-License-Identifier: MIT
"""The trilogy parity ledger's script-found facts (docs/trilogy-parity-ledger.md): what each game's build contains.

  python tools/parity-ledger.py              print the generated tables (markdown)
  python tools/parity-ledger.py --json F     also write the facts as JSON to F
  python tools/parity-ledger.py --write      update docs/trilogy-parity-ledger.json and the doc's generated sections
  python tools/parity-ledger.py --check      exit 1 if the committed JSON or the doc's generated sections are stale
  python tools/parity-ledger.py --detail     per module: active lines, gates and handle counts per game (not committed)

What it reads, with no compiler and no game:
- hosts: CMakeLists.txt's `mevr_add_plugin(game)` run per game with the default options (MEVR_HALCYON, MEVR_MAIN_MENU on,
  the LE2 diagnostic host off, hot reload on): the src/plugin and src/core .cpp files each host compiles, the Halcyon
  library it links and the defines it gets (MEVR_WITH_HALCYON[_LE2|_LE3], MEVR_WITH_MAIN_MENU).
- services: src/plugin/ModLoader.cpp preprocessed with each host's defines; `BuildServices()` applied in order (each
  `With*` helper's `services.x = ...` assignments, direct assignments, `= nullptr` unsets): which HostServices fields
  each host serves and from which helper.
- modules: every src/game and src/engine .cpp (with the .inc files it includes) preprocessed per game (MEVR_GAME_LEx;
  an in-file `#define` counts), one status per game:
    run           its engine access runs there: on LE1 any ue3 gate or handle; on LE2/LE3 `ue3::runtime::Up()` or handles
                  declared `ue3::kEveryGame` (with "part": under 30% of the largest game's build of it; with "N ME1-only
                  handles": default-reach handles compiled in, which never resolve there: ME1 paths behind runtime checks);
    ready         only `ue3::runtime::Ready()` or default-reach handles: it stands down off LE1 (kModulesOnLayer false);
    out           no ue3 gate or handle in this game's build while another game's build has one: the engine part is
                  compiled out (#if) and what remains is logic or a stub;
    ungated       no ue3 gate or handle in any game's build (pure logic, or native hooks gated by their addresses);
    unreferenced  no other mod file of this game's build names its namespace, while another game's does;
    unbuilt       not in this game's mod sources (CMake: a game-only file, or the LE2 diagnostic host's);
  and "LExSDKv2" where the game's build calls engine/Legacy (LExSDKv2's reads: not in an SDK-free players' build).
- seams: each Halcyon tree's MevrBridge functions declared in its mevr_bridge.h (the sequels' also in
  src/halcyon/SequelPresentation.h) and those its own sources call.
- names (needs LExSDKv2, see tools/le2-name-audit.py): per module, the engine names (classes, functions, fields) the
  LE2 and LE3 builds name that the game lacks, and the names ME1's build names that the sequel lacks (the porting gap).
  Without the SDK this part is skipped and --check doesn't compare it.

The judgement (shared / adapted / forked / missing / n/a, verified or not) is the doc's, not this script's: these tables
are the evidence it cites. The script only counts the doc's cells (the "totals" section: every table whose header has
`id`, `LE2` and `LE3`; a cell starts `**kind**`, then V or U for shared/adapted/forked) and lists any cell not in that form. ctest `mevr-parity-ledger` runs --check, so a change to a host list, a service, a module's
gate or a seam fails until `--write` is run and the doc's rows are looked at again.
"""
import argparse
import importlib.util
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GAMES = ('LE1', 'LE2', 'LE3')
DOC = os.path.join(ROOT, 'docs', 'trilogy-parity-ledger.md')
JSON_OUT = os.path.join(ROOT, 'docs', 'trilogy-parity-ledger.json')
SECTIONS = ('totals', 'summary', 'hosts', 'core', 'services', 'modules', 'seams', 'names')


def read(path):
    with open(path, encoding='utf-8', errors='replace') as h:
        return h.read()


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, '/')


# ---------------------------------------------------------------------------------------------------------------- C text
def strip_comments(text):
    """C/C++ comments removed (string and char literals kept, newlines kept so line numbers hold)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j))
            i = j
        elif c in '"\'':
            # a raw string R"tag(...)tag" is taken whole
            if c == '"' and i > 0 and text[i - 1] == 'R':
                m = re.match(r'"([^(\s]*)\(', text[i:])
                if m:
                    end = text.find(')' + m.group(1) + '"', i)
                    end = n if end < 0 else end + len(m.group(1)) + 2
                    out.append(text[i:end])
                    i = end
                    continue
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def eval_condition(expr, defines):
    """A #if / #elif expression over defined(X), !, &&, ||, parentheses and integer literals."""
    e = re.sub(r'defined\s*\(\s*(\w+)\s*\)', lambda m: ' 1 ' if m.group(1) in defines else ' 0 ', expr)
    e = re.sub(r'defined\s+(\w+)', lambda m: ' 1 ' if m.group(1) in defines else ' 0 ', e)
    e = e.replace('&&', ' and ').replace('||', ' or ')
    e = re.sub(r'!(?!=)', ' not ', e)
    e = re.sub(r'\b(?!and\b|or\b|not\b)[A-Za-z_]\w*\b', lambda m: ' 1 ' if m.group(0) in defines else ' 0 ', e)
    if not re.fullmatch(r'[\s\d()andornt<>=!]*', e):
        raise ValueError(f'unsupported preprocessor expression: {expr!r}')
    return bool(eval(e, {'__builtins__': {}}))  # noqa: S307 (only digits, parentheses and boolean operators remain)


def preprocess(text, defines, include_dir=None):
    """The active lines for these defines (inactive ones blank). `#include "x.inc"` files are inlined from include_dir."""
    defs = set(defines)
    out, stack, active = [], [], True
    for line in strip_comments(text).split('\n'):
        m = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif|define|undef|include)\b(.*)', line)
        if not m:
            out.append(line if active else '')
            continue
        d, rest = m.group(1), m.group(2).strip()
        if d in ('if', 'ifdef', 'ifndef'):
            if d == 'if':
                v = active and eval_condition(rest, defs)
            elif d == 'ifdef':
                v = active and rest.split()[0] in defs
            else:
                v = active and rest.split()[0] not in defs
            stack.append([active, v])
            active = v
        elif d == 'elif':
            parent, taken = stack[-1]
            v = parent and not taken and eval_condition(rest, defs)
            stack[-1][1] = taken or v
            active = v
        elif d == 'else':
            parent, taken = stack[-1]
            active = parent and not taken
            stack[-1][1] = True
        elif d == 'endif':
            active = stack.pop()[0]
        elif d == 'define' and active:
            defs.add(rest.split()[0].split('(')[0])
            body = re.sub(r'^\w+(\([^)]*\))?', '', rest).strip()
            if body:
                out.append(body)  # a macro's body counts where it's defined (a gate inside a macro is still a gate)
                continue
        elif d == 'undef' and active:
            defs.discard(rest.split()[0])
        elif d == 'include' and active and include_dir:
            inc = re.match(r'"([^"]+\.inc)"', rest)
            if inc:
                for base in (include_dir, os.path.join(ROOT, 'src')):
                    p = os.path.join(base, inc.group(1))
                    if os.path.isfile(p):
                        out.append(preprocess(read(p), defs, os.path.dirname(p)))
                        break
        out.append('')
    return '\n'.join(out)


def code_lines(text):
    return sum(1 for line in text.split('\n') if line.strip() and not line.strip().startswith('#'))


# ------------------------------------------------------------------------------------------------------------- CMake
def cmake_commands(text):
    """(name, args) for every command, comments removed, arguments tokenised (quotes stripped, parentheses kept)."""
    lines = []
    for line in text.split('\n'):
        out, q = [], False
        for i, c in enumerate(line):
            if c == '"' and (i == 0 or line[i - 1] != '\\'):
                q = not q
            if c == '#' and not q:
                break
            out.append(c)
        lines.append(''.join(out))
    s = '\n'.join(lines)
    cmds, pos = [], 0
    pat = re.compile(r'([A-Za-z_]\w*)\s*\(')
    while True:
        m = pat.search(s, pos)
        if not m:
            break
        depth, i, q = 1, m.end(), False
        while i < len(s) and depth:
            c = s[i]
            if c == '"' and s[i - 1] != '\\':
                q = not q
            elif not q and c == '(':
                depth += 1
            elif not q and c == ')':
                depth -= 1
            i += 1
        args = re.findall(r'"(?:[^"\\]|\\.)*"|\(|\)|[^\s()"]+', s[m.end():i - 1])
        cmds.append((m.group(1).lower(), args))
        pos = i
    return cmds


def cmake_expand(items, scope):
    out = []
    for it in items:
        quoted = it.startswith('"')
        it = it[1:-1] if quoted else it
        for _ in range(8):
            new = re.sub(r'\$\{(\w+)\}', lambda m: ';'.join(scope.get(m.group(1), [])), it)
            if new == it:
                break
            it = new
        out.extend([it] if quoted else [x for x in it.split(';') if x])
    return out


def cmake_truthy(token, scope):
    if token.startswith('"'):
        v = token[1:-1]
    elif token in scope:
        v = ';'.join(scope[token])
    else:
        v = token
    return v.upper() not in ('', '0', 'OFF', 'NO', 'FALSE', 'N', 'IGNORE', 'NOTFOUND') and not v.upper().endswith('-NOTFOUND')


def cmake_condition(tokens, scope):
    """if() arguments: NOT, AND, OR, STREQUAL, TARGET (always false: no target exists while reading), parentheses."""
    toks = list(tokens)
    pos = [0]

    def value(t):
        if t.startswith('"'):
            return t[1:-1]
        return ';'.join(scope[t]) if t in scope else t

    def primary():
        t = toks[pos[0]]
        pos[0] += 1
        if t == '(':
            v = expr_or()
            pos[0] += 1
            return v
        if t == 'NOT':
            return not primary()
        if t in ('TARGET', 'EXISTS', 'DEFINED', 'COMMAND'):
            pos[0] += 1
            return False if t != 'DEFINED' else toks[pos[0] - 1] in scope
        if pos[0] < len(toks) and toks[pos[0]] in ('STREQUAL', 'EQUAL', 'MATCHES'):
            op, rhs = toks[pos[0]], toks[pos[0] + 1]
            pos[0] += 2
            if op == 'MATCHES':
                return re.search(value(rhs), value(t)) is not None
            return value(t) == value(rhs)
        return cmake_truthy(t, scope)

    def expr_and():
        v = primary()
        while pos[0] < len(toks) and toks[pos[0]] == 'AND':
            pos[0] += 1
            r = primary()
            v = v and r
        return v

    def expr_or():
        v = expr_and()
        while pos[0] < len(toks) and toks[pos[0]] == 'OR':
            pos[0] += 1
            r = expr_and()
            v = v or r
        return v

    return expr_or()


def cmake_run(cmds, scope):
    """set / list(APPEND) / if-elseif-else-endif over commands; everything else ignored."""
    stack, active = [], True
    for name, args in cmds:
        if name == 'if':
            v = active and cmake_condition(args, scope)
            stack.append([active, v])
            active = v
        elif name == 'elseif':
            parent, taken = stack[-1]
            v = parent and not taken and cmake_condition(args, scope)
            stack[-1][1] = taken or v
            active = v
        elif name == 'else':
            parent, taken = stack[-1]
            active = parent and not taken
            stack[-1][1] = True
        elif name == 'endif':
            active = stack.pop()[0]
        elif not active:
            continue
        elif name == 'set' and args:
            scope[args[0]] = cmake_expand(args[1:], scope)
        elif name == 'list' and len(args) >= 2 and args[0] == 'APPEND':
            scope.setdefault(args[1], []).extend(cmake_expand(args[2:], scope))


OPTIONS = {'MEVR_HALCYON': ['ON'], 'MEVR_MAIN_MENU': ['ON'], 'MEVR_LE2_DIAGNOSTIC': ['OFF'], 'MEVR_HOT_RELOAD': ['ON'],
           'MEVR_LE1_LESDK': ['ON'], 'MEVR_BUILD_LE1': ['ON'], 'MEVR_BUILD_LE2': ['ON'], 'MEVR_BUILD_LE3': ['ON']}


def hosts():
    cmds = cmake_commands(read(os.path.join(ROOT, 'CMakeLists.txt')))
    top, i, body = dict(OPTIONS), 0, None
    while i < len(cmds):
        name, args = cmds[i]
        if name == 'function' and args and args[0] == 'mevr_add_plugin':
            j = i + 1
            while cmds[j][0] != 'endfunction':
                j += 1
            body = cmds[i + 1:j]
            i = j
        elif name in ('function', 'macro'):
            while cmds[i][0] not in ('endfunction', 'endmacro'):
                i += 1
        elif name == 'set' and args and re.fullmatch(r'MEVR_\w*SOURCES|HALCYON_LE\d_DIR', args[0]):
            top[args[0]] = cmake_expand(args[1:], top)
        i += 1
    if body is None:
        raise SystemExit('CMakeLists.txt: no function(mevr_add_plugin game)')
    out = {}
    for game in GAMES:
        scope = {k: list(v) for k, v in top.items()}
        scope['game'] = [game]
        cmake_run(body, scope)
        host = sorted({s for s in scope.get('host_sources', []) if s.endswith('.cpp')})
        mod = sorted({s for s in scope.get('mod_sources', []) if s.endswith('.cpp')})
        defines = [f'MEVR_GAME_{game}']
        for flag, define in (('with_halcyon', 'MEVR_WITH_HALCYON'), ('with_halcyon_le2', 'MEVR_WITH_HALCYON_LE2'),
                             ('with_halcyon_le3', 'MEVR_WITH_HALCYON_LE3'), ('with_main_menu', 'MEVR_WITH_MAIN_MENU')):
            if cmake_truthy(flag, scope):
                defines.append(define)
        renderer = next((lib for flag, lib in (('with_halcyon', 'halcyon-le1'), ('with_halcyon_le2', 'halcyon-le2'),
                                               ('with_halcyon_le3', 'halcyon-le3')) if cmake_truthy(flag, scope)), None)
        out[game] = {
            'defines': defines,
            'renderer': renderer,
            'plugin': sorted({os.path.splitext(os.path.basename(s))[0] for s in host if s.startswith('src/plugin/')}),
            'core': sorted({s[len('src/core/'):-4] for s in host if s.startswith('src/core/')}),
            'other': sorted({s[len('src/'):-4] for s in host if s.startswith('src/') and not s.startswith(('src/plugin/', 'src/core/'))}),
            'modExtra': sorted({s[len('src/'):-4] for s in mod} - {s[len('src/'):-4] for s in top.get('MEVR_MOD_SOURCES', [])
                                                                    if s.endswith('.cpp')}),
            # the src/game and src/engine files the game's mod compiles (the default, hot-reload build)
            'modModules': sorted(s[len('src/'):-4] for s in mod if s.startswith(('src/game/', 'src/engine/'))),
        }
    return out


# ---------------------------------------------------------------------------------------------------------- services
META_FIELDS = {'version', 'log', 'servicesSize'}


def services(host_facts):
    src = read(os.path.join(ROOT, 'src', 'plugin', 'ModLoader.cpp'))
    out = {}
    for game in GAMES:
        text = preprocess(src, host_facts[game]['defines'])
        helpers = {}
        for m in re.finditer(r'HostServices\s+(\w+)\s*\(\s*(?:HostServices\s+services\s*)?\)\s*\{', text):
            depth, i = 1, m.end()
            while depth:
                depth += {'{': 1, '}': -1}.get(text[i], 0)
                i += 1
            helpers[m.group(1)] = text[m.end():i - 1]
        build = re.search(r'HostServices\s+BuildServices\s*\(\s*\)\s*\{', text)
        depth, i = 1, build.end()
        while depth:
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        body = text[build.end():i - 1]
        served = {}

        def assign(block, source):
            for f, rhs in re.findall(r'services\.(\w+)\s*=\s*([^;]+);', block):
                if f in META_FIELDS or f.endswith('Size'):
                    continue
                if rhs.strip() == 'nullptr':
                    served.pop(f, None)
                else:
                    served[f] = source

        applied = []
        for stmt in re.split(r';', body):
            stmt = stmt.strip()
            m = re.match(r'(?:HostServices\s+)?services\s*=\s*(.+)$', stmt, re.S)
            if m:
                for helper in reversed(re.findall(r'\b(With\w+|BaseServices)\s*\(', m.group(1))):
                    applied.append(helper)
                    assign(helpers.get(helper, ''), helper)
            elif stmt.startswith('services.') or 'services.' in stmt.split('\n')[-1]:
                assign(stmt[stmt.index('services.'):] + ';', 'BuildServices')
        out[game] = {'helpers': applied, 'fields': dict(sorted(served.items()))}
    return out


# ----------------------------------------------------------------------------------------------------------- modules
HANDLE = re.compile(r'\b(ClassRef|FieldRef|FunctionRef)\s+\w+\s*\{\s*"([^"]*)"\s*(?:,\s*"([^"]*)"\s*)?(?:,\s*([^}]*))?\}')


def handle_reaches(text):
    """[(engine name, every game?)] for the root handles a text declares: ClassRef/FunctionRef{"path"[, reach]} and
    FieldRef{"Pkg.Class", "Field"[, reach]} (name "Pkg.Class:Field"). A reach is every game if it says kEveryGame, or names
    a `constexpr auto x = ue3::kEveryGame` of the same text."""
    every_names = set(re.findall(r'constexpr\s+auto\s+(\w+)\s*=\s*ue3::(?:kEveryGame|Reach::EveryGame)', text))
    out = []
    for kind, path, field, extra in HANDLE.findall(text):
        every = bool(extra) and ('EveryGame' in extra or any(re.search(r'\b' + n + r'\b', extra) for n in every_names))
        out.append((f'{path}:{field}' if field else path, every))
    return out


def module_files():
    files = []
    for sub in ('game', 'engine'):
        d = os.path.join(ROOT, 'src', sub)
        for f in sorted(os.listdir(d)):
            if f.endswith('.cpp'):
                files.append(os.path.join(d, f))
    return files


def namespace_of(text):
    m = re.search(r'^namespace\s+mevr::((?:game|engine)(?:::\w+)*)', text, re.M)
    return m.group(1) if m else None


def modules(host_facts):
    files = module_files()
    mod_cpp = os.path.join(ROOT, 'src', 'mod', 'Mod.cpp')
    texts = {}  # (game, path) -> preprocessed text
    for game in GAMES:
        defines = [f'MEVR_GAME_{game}']  # the mod's build (hot reload): no host defines
        for p in files + [mod_cpp]:
            texts[(game, p)] = preprocess(read(p), defines, os.path.dirname(p))
    out = {}
    for p in files:
        raw = read(p)
        ns = namespace_of(raw)
        name = rel(p)[len('src/'):-4]
        leaf = ns.split('::', 1)[1] if ns and '::' in ns else None
        lines = {g: code_lines(texts[(g, p)]) for g in GAMES}
        top = max(lines.values()) or 1
        entry = {'namespace': ns, 'games': {}}
        refs = {}
        for g in GAMES:
            refs[g] = False
            if leaf:
                pat = re.compile(r'\b' + re.escape(leaf) + r'::')
                built = [q for q in files if rel(q)[len('src/'):-4] in host_facts[g]['modModules']] + [mod_cpp]
                refs[g] = any(pat.search(texts[(g, q)]) for q in built if q != p)
        referenced_somewhere = any(refs.values())
        engine = {}
        for g in GAMES:
            t = texts[(g, p)]
            hs = handle_reaches(t)
            every = sum(1 for _, r in hs if r)
            up = 'runtime::Up()' in t or 'LayerReady()' in t
            ready = 'runtime::Ready()' in t
            engine[g] = (up, ready, len(hs), every, bool(re.search(r'\blegacy::\w', t)))
        for g in GAMES:
            up, ready, handles, every, legacy = engine[g]
            has_engine = up or ready or handles
            others = any(any(engine[o][:3]) for o in GAMES if o != g)
            if name not in host_facts[g]['modModules']:
                status = 'unbuilt'
            elif leaf and referenced_somewhere and not refs[g]:
                status = 'unreferenced'
            elif not has_engine:
                # compiled out: a sequel without ME1's engine part, or any game's build that is a fraction of another's
                status = 'out' if others and ((g != 'LE1' and any(engine['LE1'][:3])) or lines[g] < 0.3 * top) else 'ungated'
            elif g == 'LE1' or up or every:
                status = 'run'
            else:
                status = 'ready'
            part = status == 'run' and lines[g] < 0.3 * top
            le1_reach = handles - every if (g != 'LE1' and status == 'run') else 0
            entry['games'][g] = {'status': status, 'part': part, 'le1ReachHandles': le1_reach,
                                 'legacy': legacy and status != 'unbuilt',
                                 'detail': {'lines': lines[g], 'up': up, 'ready': ready, 'handles': handles, 'everyGame': every}}
        out[name] = entry
    return out, texts


# ------------------------------------------------------------------------------------------------------------- seams
def bridge_declarations(text):
    """Function names declared at the namespace level of `namespace ...::MevrBridge`."""
    text = strip_comments(text)
    names = []
    for m in re.finditer(r'namespace\s+[\w:]*MevrBridge\s*\{', text):
        depth, i = 1, m.end()
        line = []
        while i < len(text) and depth:
            c = text[i]
            if c == '{':
                if depth == 1:
                    stmt = ''.join(line)
                    fn = re.search(r'(?:^|[\s\*&])([A-Z]\w*)\s*\([^;]*$', stmt.strip(), re.S)
                    if fn and not re.match(r'\s*(struct|enum|class|namespace)\b', stmt):
                        names.append(fn.group(1))
                    line = []
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 1:
                    line = []
            elif depth == 1:
                if c == ';':
                    stmt = ''.join(line)
                    fn = re.search(r'(?:^|[\s\*&])([A-Z]\w*)\s*\(', stmt.strip())
                    if fn and not re.match(r'\s*(struct|enum|class|using|typedef)\b', stmt.strip()):
                        names.append(fn.group(1))
                    line = []
                else:
                    line.append(c)
            i += 1
    return names


def seams():
    out = {}
    for g, tree in (('LE1', 'le1'), ('LE2', 'le2'), ('LE3', 'le3')):
        d = os.path.join(ROOT, 'src', 'halcyon', tree)
        decl_files = [os.path.join(d, 'mevr_bridge.h')]
        if g != 'LE1':
            decl_files.append(os.path.join(ROOT, 'src', 'halcyon', 'SequelPresentation.h'))
        declared = []
        for f in decl_files:
            declared += bridge_declarations(read(f))
        used, marks = set(), {}
        for f in sorted(os.listdir(d)):
            if f.endswith(('.cpp', '.h')) and f != 'mevr_bridge.h':
                t = read(os.path.join(d, f))
                used |= set(re.findall(r'MevrBridge::(\w+)\s*\(', strip_comments(t)))
                n = t.count('[MEVR]')
                if n:
                    marks[f] = n
        out[g] = {'declared': sorted(set(declared)), 'used': sorted(used & set(declared)),
                  'usedUndeclared': sorted(used - set(declared)), 'marks': marks}
    return out


# ------------------------------------------------------------------------------------------------------------- names
def load_name_audit():
    spec = importlib.util.spec_from_file_location('le2_name_audit', os.path.join(ROOT, 'tools', 'le2-name-audit.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def names(module_facts, texts):
    audit = load_name_audit()
    sdk = audit.sdk_dir(ROOT)
    if sdk is None:
        return None
    out = {}
    for p in module_files():
        mod = rel(p)[len('src/'):-4]
        entry = {}
        le1_names = {n for n in audit.names_in(texts[('LE1', p)]) if audit.is_engine_name(n, sdk)}
        for g in ('LE2', 'LE3'):
            built = module_facts[mod]['games'][g]['status'] != 'unbuilt'
            t = texts[(g, p)] if built else ''
            own = {n for n in audit.names_in(t) if audit.is_engine_name(n, sdk)}
            every = {n for n, e in handle_reaches(t) if e}
            lacks = {n for n in own if not audit.lookup(n, g, sdk)[1]}
            entry[g] = {
                # declared ue3::kEveryGame in this game's build: the lookup is tried there and fails (or the SDK list is wrong)
                'everyGameLacks': sorted(lacks & every),
                # any other literal of this game's build: ME1-reach handles (inert there), CallFunction paths, log text...
                'otherLacks': sorted(lacks - every),
                # what ME1's build names that the sequel lacks: the porting gap
                'fromLe1Lacks': sorted(n for n in le1_names if audit.lookup(n, 'LE1', sdk)[1] and not audit.lookup(n, g, sdk)[1]),
            }
        if any(any(entry[g].values()) for g in ('LE2', 'LE3')):
            out[mod] = entry
    return out


# ---------------------------------------------------------------------------------------------------------- markdown
def table(header, rows):
    lines = ['| ' + ' | '.join(header) + ' |', '|' + '|'.join('---' for _ in header) + '|']
    lines += ['| ' + ' | '.join(str(c) for c in r) + ' |' for r in rows]
    return '\n'.join(lines)


def yes(flag):
    return 'yes' if flag else '-'


def render(facts):
    md = {}
    h, sv, mods, sm, nm = facts['hosts'], facts['services'], facts['modules'], facts['seams'], facts.get('names')
    # summary
    status_counts = {g: {} for g in GAMES}
    for m in mods.values():
        for g in GAMES:
            s = m['games'][g]['status']
            status_counts[g][s] = status_counts[g].get(s, 0) + 1
    rows = []
    for g in GAMES:
        sc = status_counts[g]
        rows.append([g, h[g]['renderer'] or '-', len(h[g]['plugin']), len(h[g]['core']), len(sv[g]['fields']),
                     ', '.join(f'{k} {sc[k]}' for k in ('run', 'ready', 'out', 'ungated', 'unreferenced', 'unbuilt') if k in sc),
                     len(sm[g]['declared'])])
    md['summary'] = table(['game', 'renderer', 'host plugin files', 'host core files', 'services served',
                           'src/game + src/engine modules by status', 'MevrBridge seams'], rows)
    # hosts
    allp = sorted(set().union(*(h[g]['plugin'] for g in GAMES)))
    md['hosts'] = table(['src/plugin', 'LE1', 'LE2', 'LE3'], [[f'`{p}`'] + [yes(p in h[g]['plugin']) for g in GAMES] for p in allp])
    extra = [f'- {g}: also `{"`, `".join(h[g]["other"])}`' for g in GAMES if h[g]['other']]
    extra += [f'- {g} mod extras: `{"`, `".join(h[g]["modExtra"])}`' for g in GAMES if h[g]['modExtra']]
    if extra:
        md['hosts'] += '\n\n' + '\n'.join(extra)
    allc = sorted(set().union(*(h[g]['core'] for g in GAMES)))
    everywhere = [c for c in allc if all(c in h[g]['core'] for g in GAMES)]
    md['core'] = (f'In all three hosts ({len(everywhere)}): `' + '`, `'.join(everywhere) + '`.\n\n'
                  + table(['src/core (host)', 'LE1', 'LE2', 'LE3'],
                          [[f'`{c}`'] + [yes(c in h[g]['core']) for g in GAMES] for c in allc if c not in everywhere]))
    # services
    allf = sorted(set().union(*(sv[g]['fields'] for g in GAMES)))
    md['services'] = ('Helpers applied, in order:\n\n' + '\n'.join(f'- {g}: ' + ', '.join(f'`{x}`' for x in sv[g]['helpers']) for g in GAMES)
                      + '\n\n' + table(['HostServices field', 'LE1', 'LE2', 'LE3'],
                                       [[f'`{f}`'] + [sv[g]['fields'].get(f, '-') for g in GAMES] for f in allf]))
    # modules
    rows = []
    for name, m in sorted(mods.items()):
        cells = []
        for g in GAMES:
            e = m['games'][g]
            notes = (['part'] if e['part'] else []) + ([f'{e["le1ReachHandles"]} ME1-only handles'] if e['le1ReachHandles'] else []) \
                + (['LExSDKv2'] if e['legacy'] else [])
            cells.append(e['status'] + (f' ({", ".join(notes)})' if notes else ''))
        rows.append([f'`{name}`'] + cells)
    md['modules'] = table(['module', 'LE1', 'LE2', 'LE3'], rows)
    # seams
    alls = sorted(set().union(*(sm[g]['declared'] for g in GAMES)))

    def seam_cell(g, s):
        if s not in sm[g]['declared']:
            return '-'
        return 'yes' if s in sm[g]['used'] else 'declared'
    md['seams'] = table(['MevrBridge seam', 'LE1', 'LE2', 'LE3'], [[f'`{s}`'] + [seam_cell(g, s) for g in GAMES] for s in alls])
    undeclared = [f'- {g}: `{"`, `".join(sm[g]["usedUndeclared"])}`' for g in GAMES if sm[g]['usedUndeclared']]
    if undeclared:
        md['seams'] += '\n\nCalled as `MevrBridge::X` but declared elsewhere (a header of the tree):\n\n' + '\n'.join(undeclared)
    # names
    if nm is not None:
        rows = [[f'`{mod}`'] + [len(e[g][k]) for g in ('LE2', 'LE3') for k in ('everyGameLacks', 'otherLacks', 'fromLe1Lacks')]
                for mod, e in sorted(nm.items())]
        md['names'] = table(['module', 'LE2: every-game handles', 'LE2: other literals', 'ME1 build vs ME2',
                             'LE3: every-game handles', 'LE3: other literals', 'ME1 build vs ME3'], rows)
        own = [(mod, g, e[g]['everyGameLacks']) for mod, e in sorted(nm.items()) for g in ('LE2', 'LE3') if e[g]['everyGameLacks']]
        if own:
            md['names'] += ('\n\nHandles a sequel build declares `ue3::kEveryGame` whose name that game\'s SDK list lacks (the '
                            'lookup is tried there: a failure, or a gap in the generated SDK list; ReflectCheck decides):\n\n'
                            + '\n'.join(f'- {g} `{mod}`: ' + ', '.join(f'`{n}`' for n in lst) for mod, g, lst in own))
    return md


CELL = re.compile(r'^\*\*(shared|adapted|forked|missing|n/a)\*\*(?:\s+([VU])\b)?')
KINDS = ('shared', 'adapted', 'forked', 'missing', 'n/a')


def ledger_totals(doc):
    """Counts of the doc's hand-written cells: every table whose header has `id`, `LE2` and `LE3` columns; a cell starts
    with **kind** and, for shared/adapted/forked, V (verified) or U (unverified)."""
    counts = {g: {k: 0 for k in KINDS} for g in ('LE2', 'LE3')}
    checks = {g: {'V': 0, 'U': 0} for g in ('LE2', 'LE3')}
    bad, rows, header = [], 0, None
    for line in doc.split('\n'):
        if not line.startswith('|'):
            header = None
            continue
        cells = [c.strip() for c in re.split(r'(?<!\\)\|', line.strip().strip('|'))]
        if header is None:
            header = cells if cells and cells[0] == 'id' and 'LE2' in cells and 'LE3' in cells else []
            continue
        if not header or set(line.replace('|', '').strip()) <= set('-: '):
            continue
        rows += 1
        for g in ('LE2', 'LE3'):
            cell = cells[header.index(g)] if header.index(g) < len(cells) else ''
            m = CELL.match(cell)
            if not m or (m.group(1) in ('shared', 'adapted', 'forked') and not m.group(2)):
                bad.append(f'{cells[0]} {g}: "{cell[:40]}"')
                continue
            counts[g][m.group(1)] += 1
            if m.group(2) and m.group(1) in ('shared', 'adapted', 'forked'):
                checks[g][m.group(2)] += 1
    return rows, counts, checks, bad


def render_totals(doc):
    rows, counts, checks, bad = ledger_totals(doc)
    out = table(['game'] + list(KINDS) + ['present and verified (V)', 'present, unverified (U)'],
                [[g] + [counts[g][k] for k in KINDS] + [checks[g]['V'], checks[g]['U']] for g in ('LE2', 'LE3')])
    out = f'{rows} rows.\n\n' + out
    if bad:
        out += '\n\nCells not in the ledger\'s form (`**kind** V|U ...`):\n\n' + '\n'.join(f'- {b}' for b in bad)
    return out


def committed(facts):
    """The checked part of the facts: no per-file counts that move with every edit."""
    c = {'hosts': facts['hosts'], 'services': facts['services'],
         'modules': {k: {'namespace': v['namespace'],
                         'games': {g: {'status': e['status'], 'part': e['part'], 'le1ReachHandles': e['le1ReachHandles'],
                                       'legacy': e['legacy']}
                                   for g, e in v['games'].items()}} for k, v in facts['modules'].items()},
         'seams': {g: {k: v for k, v in s.items() if k != 'marks'} for g, s in facts['seams'].items()}}
    if facts.get('names') is not None:
        c['names'] = facts['names']
    return c


def gather():
    h = hosts()
    mods, texts = modules(h)
    facts = {'hosts': h, 'services': services(h), 'modules': mods, 'seams': seams()}
    facts['names'] = names(mods, texts)
    return facts


def doc_sections(doc):
    return {m.group(1): m.group(2).strip('\n') for m in
            re.finditer(r'<!-- parity:(\w+) -->\n(.*?)<!-- /parity:\1 -->', doc, re.S)}


def put_sections(doc, md):
    for k, v in md.items():
        doc, n = re.subn(r'(<!-- parity:' + k + r' -->\n).*?(<!-- /parity:' + k + r' -->)',
                         lambda m: m.group(1) + v + '\n' + m.group(2), doc, flags=re.S)
    return doc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', help='write the facts (the committed part) as JSON here')
    ap.add_argument('--write', action='store_true', help='update docs/trilogy-parity-ledger.json and the doc sections')
    ap.add_argument('--check', action='store_true', help='fail if the committed JSON or the doc sections are stale')
    ap.add_argument('--detail', action='store_true', help='print per-module counts per game and the [MEVR] marks')
    args = ap.parse_args(argv)
    facts = gather()
    md = render(facts)
    data = committed(facts)
    text = json.dumps(data, indent=1, sort_keys=True) + '\n'
    if args.detail:
        for name, m in sorted(facts['modules'].items()):
            print(name, ' | '.join(f'{g}: {e["status"]} {e["detail"]}' for g, e in m['games'].items()))
        for g, s in facts['seams'].items():
            print(g, '[MEVR] marks:', s['marks'])
        return 0
    if args.json:
        with open(args.json, 'w', encoding='utf-8', newline='\n') as h:
            h.write(text)
    if args.write:
        with open(JSON_OUT, 'w', encoding='utf-8', newline='\n') as h:
            h.write(text)
        doc = read(DOC)
        md['totals'] = render_totals(doc)
        with open(DOC, 'w', encoding='utf-8', newline='\n') as h:
            h.write(put_sections(doc, md))
        missing = [k for k in md if f'<!-- parity:{k} -->' not in doc]
        if missing:
            print(f'the doc has no section for: {", ".join(missing)}', file=sys.stderr)
        print(f'wrote {rel(JSON_OUT)} and {rel(DOC)}' + ('' if facts['names'] is not None else
                                                         ' (no LExSDKv2: the names section kept as it was)'))
        return 0
    if args.check:
        problems = []
        try:
            old = json.loads(read(JSON_OUT))
        except (OSError, ValueError) as e:
            old, problems = {}, [f'{rel(JSON_OUT)}: {e}']
        if facts['names'] is None:
            old.pop('names', None)
            print('LExSDKv2 not checked out: the names part is not compared', file=sys.stderr)
        for k in sorted(set(old) | set(data)):
            if old.get(k) != data.get(k):
                problems.append(f'{rel(JSON_OUT)}: "{k}" differs from the sources')
        doc = read(DOC)
        have = doc_sections(doc)
        md['totals'] = render_totals(doc)
        for k, v in md.items():
            if have.get(k) != v.strip('\n'):
                problems.append(f'{rel(DOC)}: generated section "{k}" is stale or missing')
        if problems:
            print('\n'.join(problems))
            print('Run `python tools/parity-ledger.py --write`, then read the ledger rows the change touches '
                  '(docs/trilogy-parity-ledger.md) and update their cells.')
            return 1
        print('parity ledger: up to date')
        return 0
    if os.path.isfile(DOC):
        md['totals'] = render_totals(read(DOC))
    for k in SECTIONS:
        if k in md:
            print(f'## {k}\n\n{md[k]}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
