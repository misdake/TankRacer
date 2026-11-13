use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{
    archive::Archive,
    config::{ResolveContext, Surfaces, Tank},
};
use tankracer_game::drive::{Environment, Error, Input, State, Tuning};
use tankracer_game::{math::Matrix, motion, powertrain, velocity};

struct Random(u32);
impl Random {
    fn word(&mut self) -> u32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        self.0
    }
    fn choose(&mut self, values: &[i32]) -> i32 {
        values[self.word() as usize % values.len()]
    }
}
fn state(s: &State) -> [u32; 16] {
    [
        s.velocity[0],
        s.velocity[1],
        s.velocity[2],
        s.angular[0],
        s.angular[1],
        s.angular[2],
        s.track_speed,
        s.boost_target,
        s.engine_display,
        s.slip,
        i32::from(s.was_active),
        i32::from(s.gear),
        s.lateral_velocity,
        s.acceleration[0],
        s.acceleration[1],
        s.load,
    ]
    .map(|v| v as u32)
}
fn tuning_words(tuning: &Tuning) -> Vec<u32> {
    let mut out = tuning.gear_limits.map(|v| v as u32).to_vec();
    out.extend(tuning.gear_offsets.map(|v| v as u32));
    out.extend(tuning.gear_gain.map(|v| v.packed()));
    out.extend(
        [
            tuning.engine_damping,
            tuning.boost_approach,
            tuning.boost_decay,
        ]
        .map(|v| v.packed()),
    );
    out
}
fn request(
    s: &State,
    input: &Input,
    env: &Environment,
    tuning: &Tuning,
    context: ResolveContext,
) -> Vec<u32> {
    let mut out = vec![
        context.map,
        u32::from(context.special),
        context.circuit_power_mul as u32,
        env.dt as u32,
        env.mode as u32,
        u32::from(env.player),
        u32::from(env.active),
        u32::from(env.launch_eligible),
        env.materials[0] as u32,
        env.materials[1] as u32,
    ];
    out.extend(env.kinds.map(|v| v as u32));
    out.extend(env.contact_y.map(|v| v as u32));
    out.extend([
        env.airborne as u32,
        env.boost_charge as u32,
        env.boost_power as u32,
        env.extra_drive as u32,
    ]);
    out.extend([
        input.throttle as u32,
        input.brake as u32,
        input.steering as u32,
        input.action_flags,
        u32::from(input.alternate),
        input.alternate_lateral as u32,
        input.alternate_rotation as u32,
    ]);
    out.extend(state(s));
    out.extend(tuning.gear_limits.map(|v| v as u32));
    out.extend(tuning.gear_offsets.map(|v| v as u32));
    out.extend(tuning.gear_gain.map(|v| v.packed()));
    out.extend(
        [
            tuning.engine_damping,
            tuning.boost_approach,
            tuning.boost_decay,
        ]
        .map(|v| v.packed()),
    );
    assert_eq!(out.len(), 63);
    out
}

#[test]
#[ignore = "requires the native drive oracle; set TANKRACER_DRIVE_REFERENCE"]
fn authored_tanks_weather_controls_gears_and_short_sequences_match_c() {
    compare(false);
}
#[test]
#[ignore = "requires extended native drive oracle; set TANKRACER_DRIVE_REFERENCE"]
fn world_body_drive_yaw_and_suspension_velocity_match_c() {
    compare(true);
}
fn compare(powered: bool) {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let folder = root.join(if powered {
        "build/tests/powertrain"
    } else {
        "build/tests/drive"
    });
    fs::create_dir_all(&folder).unwrap();
    let bytes = fs::read(root.join("assets/game/DATA/FASTFILE.DAT")).unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    let surface_file = folder.join("SURFS.DAT");
    let surface_bytes = archive.get("SURFS.DAT").unwrap();
    fs::write(&surface_file, surface_bytes).unwrap();
    let surfaces = Surfaces::decode(surface_bytes).unwrap();
    let mut r = Random(0x430c51);
    let boundary = [
        i32::MIN,
        i32::MIN + 1,
        -314488,
        -300000,
        -2501,
        -1024,
        -1,
        0,
        1,
        1023,
        1024,
        2500,
        2501,
        299999,
        300000,
        314488,
        i32::MAX,
    ];
    let mut total = 0;
    let mut invalid_weather = 0;
    let mut gear_limits = 0;
    let mut gears = [0; 5];
    let mut launch_cases = 0;
    let mut weather_cases = [0; 7];
    let mut hill_cases = 0;
    let mut cached_precision = 0;
    let mut yaw_applied = 0;
    let mut yaw_blocked = 0;
    for index in 1..=15 {
        let name = format!("TANK{index}.DAT");
        let source = archive.get(&name).unwrap();
        let tank = Tank::decode(source).unwrap();
        let tank_file = folder.join(name);
        fs::write(&tank_file, source).unwrap();
        let mut binary = Vec::new();
        let mut expected = Vec::new();
        let mut labels = Vec::new();
        for case in 0..if powered { 80 } else { 160 } {
            let mut s = State {
                velocity: std::array::from_fn(|_| r.choose(&boundary)),
                angular: std::array::from_fn(|_| r.choose(&boundary)),
                track_speed: r.choose(&boundary),
                boost_target: if case % 7 == 0 { 314488 } else { 0 },
                engine_display: r.choose(&boundary),
                slip: r.choose(&boundary),
                was_active: (case % 3) as u8,
                gear: -1,
                lateral_velocity: 99,
                acceleration: [77, -88],
                load: r.choose(&boundary),
            };
            let mut tuning = Tuning::default();
            if case % 29 == 0 {
                tuning.gear_limits = [0; 5];
                s.track_speed = 100;
            }
            if case % 31 == 0 {
                tuning.engine_damping.shift = 255;
                tuning.boost_approach.fraction = 255;
                tuning.boost_decay.step = -32768;
            }
            let input = Input {
                throttle: if case % 5 == 0 {
                    0
                } else {
                    r.choose(&boundary)
                },
                brake: if case % 3 == 0 {
                    0
                } else {
                    r.choose(&boundary)
                },
                steering: r.choose(&boundary),
                action_flags: [
                    0,
                    1,
                    2,
                    1 | 8,
                    1 | 16,
                    1 | 32,
                    2 | 8,
                    2 | 16,
                    2 | 32,
                    3 | 56,
                ][case % 10],
                alternate: case % 4 == 0,
                alternate_lateral: r.choose(&boundary) as i16,
                alternate_rotation: r.choose(&boundary) as i16,
            };
            let mut env = Environment {
                dt: [0, 1, 16, 17, 33, -1, i32::MAX, i32::MIN][case % 8],
                mode: [0, 1, 2, 4, 5, -1][case % 6],
                player: case % 3 != 0,
                active: case % 4 != 0,
                launch_eligible: case % 7 == 0,
                materials: [case % 10, (case * 3 + index) % 10],
                kinds: if case % 7 == 0 {
                    [4, 3, 1, 0]
                } else {
                    [0, 1, 2, 3]
                },
                contact_y: std::array::from_fn(|_| r.choose(&boundary)),
                airborne: (case % 5) as i8,
                boost_charge: [-1, 0, 2500, 2501, 32767][case % 5],
                boost_power: r.choose(&boundary),
                extra_drive: r.choose(&boundary),
            };
            let context = ResolveContext {
                special: case % 4 == 0,
                player: env.player,
                mode: env.mode as u32,
                map: if case % 7 == 0 { 7 } else { 0 },
                circuit_power_mul: 133,
                ambient_battle: None,
            };
            let resolved = tank.resolve(context).unwrap();
            let mut motion_state = motion::State {
                velocity: std::array::from_fn(|_| r.choose(&boundary)),
                angular: s.angular,
                acceleration: s.acceleration,
                rates: motion::Rates {
                    track_slip: s.slip,
                    lateral_velocity: s.lateral_velocity,
                    ..motion::Rates::default()
                },
                ..motion::State::default()
            };
            motion_state.poses.reference =
                Matrix::from_angles(std::array::from_fn(|_| r.word() & 65535));
            let mut power = powertrain::State {
                local_velocity: s.velocity,
                track_speed: s.track_speed,
                boost_target: s.boost_target,
                engine_display: s.engine_display,
                was_active: s.was_active,
                gear: s.gear,
                load: s.load,
            };
            let vertical_delta = r.choose(&boundary);
            let yaw_lock = [-1, 0, 1, i32::MAX][case % 4];
            let mut yaw = velocity::YawConfig::from(&resolved.tank.drive);
            if case % 7 == 0 {
                yaw.axis = std::array::from_fn(|_| r.choose(&boundary));
            }
            for tick in 0..4 {
                if tick == 2 {
                    env.active = !env.active;
                }
                launch_cases += usize::from(env.active && env.launch_eligible && s.was_active != 1);
                binary.extend(
                    request(&s, &input, &env, &tuning, context)
                        .into_iter()
                        .flat_map(u32::to_le_bytes),
                );
                if powered {
                    binary.extend(
                        motion_state
                            .velocity
                            .into_iter()
                            .chain(motion_state.poses.reference.0)
                            .chain([vertical_delta, yaw_lock])
                            .chain(yaw.axis)
                            .flat_map(|v| (v as u32).to_le_bytes()),
                    );
                }
                let incoming_precision = velocity::large(motion_state.velocity);
                let result = if powered {
                    let result = power.step(
                        &mut motion_state,
                        &powertrain::Config {
                            drive: &resolved.tank.drive,
                            surfaces: &surfaces,
                            tuning: &tuning,
                            yaw,
                        },
                        &powertrain::Frame {
                            input: &input,
                            drive: &env,
                            vertical_delta,
                            yaw_lock,
                        },
                    );
                    s = power.drive_state(&motion_state);
                    match result {
                        Ok(()) => Ok(()),
                        Err(powertrain::Error::Drive(e)) => Err(e),
                        Err(powertrain::Error::InvalidTimeStep) => {
                            expected.push((-2_i32) as u32);
                            expected.extend(state(&s));
                            expected.extend(motion_state.velocity.map(|v| v as u32));
                            labels.push((case, tick));
                            continue;
                        }
                    }
                } else {
                    s.step(&resolved.tank.drive, &surfaces, &tuning, &input, &env)
                };
                let code = match result {
                    Ok(()) => 0,
                    Err(Error::InvalidWeather) => {
                        invalid_weather += 1;
                        -4
                    }
                    Err(Error::GearLimit) => {
                        gear_limits += 1;
                        -3
                    }
                    Err(e) => panic!("unexpected {e:?}"),
                };
                if code == 0 {
                    if powered {
                        cached_precision += usize::from(
                            incoming_precision != velocity::large(power.local_velocity),
                        );
                        if input.alternate || yaw_lock >= 1 {
                            yaw_blocked += 1;
                        } else {
                            yaw_applied += 1;
                        }
                    }
                    gears[s.gear as usize] += 1;
                    if env.player && !input.alternate {
                        let flags = input.action_flags;
                        let slide = if flags & 3 == 0 {
                            0
                        } else {
                            (if flags & 1 != 0 { 1 } else { 4 })
                                + if flags & 8 != 0 {
                                    0
                                } else if flags & 16 != 0 {
                                    1
                                } else {
                                    2
                                }
                        };
                        weather_cases[slide] += 1;
                    }
                    hill_cases +=
                        usize::from(env.active && input.throttle == 0 && input.brake == 0);
                }
                expected.push(code as u32);
                expected.extend(state(&s));
                if powered {
                    expected.extend(motion_state.velocity.map(|v| v as u32));
                }
                labels.push((case, tick));
            }
        }
        let file = folder.join(format!("{index}.input.bin"));
        fs::write(&file, binary).unwrap();
        let mut command = Command::new(
            std::env::var_os("TANKRACER_DRIVE_REFERENCE").expect("set TANKRACER_DRIVE_REFERENCE"),
        );
        command.arg(tank_file).arg(&surface_file);
        if powered {
            command.arg("--powertrain");
        }
        let output = command
            .stdin(Stdio::from(fs::File::open(file).unwrap()))
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], if powered { b"PW01" } else { b"DR01" });
        let actual: Vec<_> = output.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(
            actual[..18],
            tuning_words(&Tuning::default()),
            "typed tuning must match original C constant catalogue"
        );
        let actual = &actual[18..];
        assert_eq!(actual.len(), expected.len());
        let words = if powered { 20 } else { 17 };
        for (i, (case, tick)) in labels.iter().enumerate() {
            assert_eq!(
                actual[i * words..(i + 1) * words],
                expected[i * words..(i + 1) * words],
                "tank={index} case={case} tick={tick}"
            );
        }
        total += labels.len();
        eprintln!(
            "tank {index}: {} exact records (powertrain={powered})",
            labels.len()
        );
    }
    assert!(
        invalid_weather > 0 && gear_limits > 0 && launch_cases > 0 && gears.iter().all(|&n| n > 0)
    );
    if powered {
        assert!(cached_precision > 0 && yaw_applied > 0 && yaw_blocked > 0);
        eprintln!(
            "precision retained across {cached_precision} threshold changes; yaw applied={yaw_applied}, blocked={yaw_blocked}"
        );
    }
    assert!(
        weather_cases.iter().all(|&n| n > 0) && hill_cases > 0,
        "weather={weather_cases:?}, hill={hill_cases}"
    );
    eprintln!(
        "{total} drive records; weather errors={invalid_weather}, gear-limit errors={gear_limits}, launches={launch_cases}, gears={gears:?}"
    );
}
