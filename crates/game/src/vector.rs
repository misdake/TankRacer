use crate::{fixed::trunc_shift, math};

pub fn normalize(v: &mut [i32; 3]) {
    let [a, b, c] = v.map(i32::wrapping_abs);
    let length = if b > a && b > c {
        b.wrapping_add(a >> 2).wrapping_add(c >> 2)
    } else if a >= b && a > c {
        a.wrapping_add(b >> 2).wrapping_add(c >> 2)
    } else {
        c.wrapping_add(a >> 2).wrapping_add(b >> 2)
    };
    if length <= 0 {
        return;
    }
    for value in v {
        *value = if length > 262144 {
            *value / (length >> 12)
        } else {
            value.wrapping_mul(4096) / length
        };
    }
}
pub fn cross(a: [i32; 3], b: [i32; 3]) -> [i32; 3] {
    std::array::from_fn(|i| {
        a[(i + 1) % 3]
            .wrapping_mul(b[(i + 2) % 3])
            .wrapping_sub(b[(i + 1) % 3].wrapping_mul(a[(i + 2) % 3]))
            >> 12
    })
}
pub fn yaw(v: [i32; 3], angle: u32) -> [i32; 3] {
    let (sine, cosine) = (math::sine(angle), math::cosine(angle));
    [
        trunc_shift(
            v[0].wrapping_mul(cosine)
                .wrapping_sub(v[2].wrapping_mul(sine)),
            12,
        ),
        v[1],
        trunc_shift(
            v[0].wrapping_mul(sine)
                .wrapping_add(v[2].wrapping_mul(cosine)),
            12,
        ),
    ]
}
