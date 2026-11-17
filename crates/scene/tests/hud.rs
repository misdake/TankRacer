//! Exact native HUD primitive geometry/state, before atlas placement.
use std::{
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_scene::hud::{self, Snapshot};
fn words(s: Snapshot) -> Vec<u32> {
    let p = hud::plan(s).unwrap();
    let mut out = vec![p.len() as u32];
    for q in p {
        let policy = hud::material(q.blend);
        assert_eq!(policy.pipeline.depth_compare, None);
        assert!(!policy.pipeline.depth_write);
        assert_eq!(policy.pipeline.cull, tankracer_render::material::Cull::None);
        assert_eq!(
            policy.pipeline.blend,
            q.blend
                .then_some(tankracer_render::material::Blend::PREMULTIPLIED_ALPHA)
        );
        out.extend([
            q.resource as u32,
            u32::from(policy.pipeline.depth_compare.is_some()),
            u32::from(policy.pipeline.depth_write),
            u32::from(policy.pipeline.blend.is_some()),
            if q.blend { 5 } else { 2 },
            if q.blend { 6 } else { 1 },
            1,
            4,
        ]);
        for (pos, uv) in q.positions.into_iter().zip(q.uv) {
            let [r, g, b, a] = q.color;
            let argb = u32::from_le_bytes([b, g, r, a]);
            out.extend([
                pos[0].to_bits(),
                pos[1].to_bits(),
                argb,
                uv[0].to_bits(),
                uv[1].to_bits(),
            ]);
        }
    }
    out
}
#[test]
fn timer_blinks_without_mutating_game_state() {
    let a = Snapshot {
        map: 0,
        countdown: 0,
        release: 0,
        elapsed: 499,
        position: [0; 3],
        speed: 0,
        needle: 0,
        cockpit: false,
        turret_yaw: 0,
    };
    assert_eq!(hud::plan(a).unwrap(), hud::plan(a).unwrap());
    let mut b = a;
    b.elapsed = 500;
    assert_ne!(hud::plan(a).unwrap(), hud::plan(b).unwrap());
    b.countdown = 1500;
    let p = hud::plan(b).unwrap();
    assert_eq!(p.iter().filter(|q| q.blend).count(), 1);
    b.countdown = 0;
    b.release = 1000;
    assert_eq!(hud::plan(b).unwrap().iter().filter(|q| q.blend).count(), 3);
}
#[test]
#[ignore = "requires TANKRACER_HUD_REFERENCE original HUD draw oracle"]
fn original_quads_uvs_colors_and_raster_states_match() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let oracle = std::env::var_os("TANKRACER_HUD_REFERENCE").expect("set HUD oracle");
    let mut fields = 0;
    for map in 0..8 {
        let cases: Vec<_> = (0..16)
            .map(|i| Snapshot {
                map,
                countdown: [3000, 2001, 2000, 1500, 1001, 1000, 1, 0][i % 8],
                release: if i >= 8 { [1000, 501, 1, 0][i % 4] } else { 0 },
                elapsed: [0, 499, 500, 59999, 60000, 5999999, 6000000, 123456][i % 8],
                position: [i as i32 * 27191 - 200000, 1234, 319100 - i as i32 * 52001],
                speed: [0, 1000, -300000, 900000][i % 4],
                needle: [0, 1000, -120000, 214999][i % 4],
                cockpit: i % 2 != 0,
                turret_yaw: (i as i16).wrapping_mul(-5419),
            })
            .collect();
        let mut input = vec![cases.len() as u32];
        let mut expected = vec![];
        for s in &cases {
            input.extend([s.countdown as u32, s.release as u32, s.elapsed as u32]);
            input.extend(s.position.map(|v| v as u32));
            input.extend([s.speed as u32, s.needle as u32]);
            input.extend([u32::from(s.cockpit), s.turret_yaw as u16 as u32]);
            expected.extend(words(*s));
        }
        let mut child = Command::new(&oracle)
            .arg(&root)
            .arg(map.to_string())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        child
            .stdin
            .take()
            .unwrap()
            .write_all(
                &input
                    .into_iter()
                    .flat_map(u32::to_le_bytes)
                    .collect::<Vec<_>>(),
            )
            .unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(output.status.success(), "map={map}");
        let actual: Vec<u32> = output
            .stdout
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        for (i, (a, b)) in actual.iter().zip(&expected).enumerate() {
            assert_eq!(*a, *b, "map={map} word={i}");
        }
        assert_eq!(actual.len(), expected.len());
        fields += actual.len();
    }
    eprintln!("HUD: 128 snapshots / {fields} original geometry, UV, color and raster-state fields");
}
