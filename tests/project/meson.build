# SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
# SPDX-License-Identifier: GPL-3.0-only

tests = [
    # project tests created for muon
    ['muon/timeout', {'should_fail': true}],
    ['muon/sizeof_invalid'],
    ['muon/str'],
    ['muon/python', {'python': true}],
    ['muon/script_module'],
    ['muon/objc and cpp', {'objc': true}],

    # project tests imported from meson unit tests
    # TODO: move this to meson-tests
    # unit
    ['unit/65 alias target', {'python': true}],
]

if dep_dict['samurai']
    ninja = 'USE_MUON'
else
    ninja = find_program('ninja', 'samu', required: false)
    if not ninja.found()
        warning('ninja/samu not found, skipping project tests')
        subdir_done()
    endif
endif

meson_tests = subproject('meson-tests', required: get_option('meson-tests'))
if meson_tests.found()
    tests += meson_tests.get_variable('tests')
else
    warning('meson-tests not found, no tests from meson will be run')
endif

test_dir = meson.current_build_dir()

foreach t : tests
    # The exit code to use when a test is skipped
    name = t[0]
    props = t.get(1, {})

    if 'skip' in props
        continue
    endif

    test_source = props.get('source_root', meson.current_source_dir()) / name
    kwargs = props.get('kwargs', {})
    suites = ['project', name.split('/')[0]]

    # skip objc tests on non darwin platforms
    # TODO: add objc / objcpp compiler detection and skip based on that instead
    if props.get('objc', false) and host_machine.system() != 'darwin'
        continue
    endif

    if props.get('python', false) and not python3.found()
        continue
    else
        suites += 'requires_python'
    endif

    skip_exit_code = '77'
    if props.get('should_fail', false)
        skip_exit_code = '1'
        kwargs += {'should_fail': true}
    endif

    skip_analyze = props.get('skip_analyze', false) ? '1' : '0'
    git_clean = props.get('git_clean', false) ? '1' : '0'
    failing = props.get('failing', false) ? '1' : '0'

    setup_options = [{}]

    if 'option_matrix' in props
        pools = []

        foreach k, vals : props['option_matrix']
            pool = []
            foreach v : vals
                pool += {k: v}
            endforeach
            pools += [pool]
        endforeach

        result = [[]]
        foreach pool : pools
            new_result = []
            foreach y : pool
                new_x = []
                foreach x : result
                    new_x += x + y
                endforeach
                new_result += new_x
            endforeach
            result = new_result
        endforeach

        setup_options = result
    endif

    foreach opts : setup_options
        optstrs = []
        foreach k, v : opts
            optstrs += f'-D@k@=@v@'
        endforeach

        test_name = ' '.join(name, optstrs)
        test_args = [
            'internal',
            'eval',
            meson.current_source_dir() / 'runner.meson',
            muon,
            ninja,
            test_source,
            test_dir / test_name,
            skip_exit_code,
            skip_analyze,
            git_clean,
            failing,
        ] + optstrs

        test(
            test_name,
            muon,
            args: test_args,
            suite: suites,
            kwargs: kwargs,
        )
    endforeach
endforeach
