//! Clamp/linear, base-level atlas. Mips/repeat textures must use separate policies.
use tankracer_assets::surface::Image;

const GUTTER: u32 = 2;
const MAX_SIDE: u32 = 2048;

#[derive(Debug, Clone, Copy)]
pub struct Rect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

#[derive(Debug)]
pub struct Atlas {
    pub width: u32,
    pub height: u32,
    pub premultiplied_rgba: Vec<u8>,
    pub regions: Vec<Rect>,
    pub white: Rect,
}

impl Atlas {
    pub fn pack(images: &[Image]) -> Result<Self, &'static str> {
        let mut sizes = vec![(1, 1)];
        sizes.extend(images.iter().map(|i| (i.width, i.height)));
        if sizes.iter().any(|&(w, h)| {
            w == 0 || h == 0 || w > MAX_SIDE - 2 * GUTTER || h > MAX_SIDE - 2 * GUTTER
        }) {
            return Err("World texture does not fit the clamp atlas policy");
        }
        let area: u64 = sizes
            .iter()
            .map(|&(w, h)| u64::from(w + 2 * GUTTER) * u64::from(h + 2 * GUTTER))
            .sum();
        let mut width = 64;
        while u64::from(width * width) < area || sizes.iter().any(|&(w, _)| w + 2 * GUTTER > width)
        {
            width *= 2;
            if width > MAX_SIDE {
                return Err("World atlas exceeds its texture-size policy");
            }
        }
        let mut order: Vec<_> = (0..sizes.len()).collect();
        order.sort_by_key(|&i| {
            (
                std::cmp::Reverse(sizes[i].1),
                std::cmp::Reverse(sizes[i].0),
                i,
            )
        });
        let (rects, height) = loop {
            let (mut x, mut y, mut row) = (0, 0, 0);
            let mut rects = vec![
                Rect {
                    x: 0,
                    y: 0,
                    width: 1,
                    height: 1
                };
                sizes.len()
            ];
            for &i in &order {
                let (w, h) = sizes[i];
                if x + w + 2 * GUTTER > width {
                    x = 0;
                    y += row;
                    row = 0;
                }
                rects[i] = Rect {
                    x: x + GUTTER,
                    y: y + GUTTER,
                    width: w,
                    height: h,
                };
                x += w + 2 * GUTTER;
                row = row.max(h + 2 * GUTTER);
            }
            let height = (y + row).next_power_of_two();
            if height <= MAX_SIDE {
                break (rects, height);
            }
            width *= 2;
            if width > MAX_SIDE {
                return Err("World atlas shelf layout exceeds 2048 pixels");
            }
        };
        let mut atlas = Self {
            width,
            height,
            premultiplied_rgba: vec![0; (width * height * 4) as usize],
            regions: rects[1..].to_vec(),
            white: rects[0],
        };
        atlas.blit(rects[0], &[255; 4]);
        for (i, image) in images.iter().enumerate() {
            if image.rgba.len() != (image.width * image.height * 4) as usize {
                return Err("Invalid atlas input span");
            }
            atlas.blit(rects[i + 1], &image.rgba);
        }
        Ok(atlas)
    }

    fn blit(&mut self, rect: Rect, source: &[u8]) {
        // Extrude clamped edges into every side/corner. RGB is premultiplied
        // before filtering, so invisible purple/black cannot stain a silhouette.
        for y in 0..rect.height + 2 * GUTTER {
            for x in 0..rect.width + 2 * GUTTER {
                let sx = x.saturating_sub(GUTTER).min(rect.width - 1);
                let sy = y.saturating_sub(GUTTER).min(rect.height - 1);
                let at = ((sy * rect.width + sx) * 4) as usize;
                let out = (((rect.y - GUTTER + y) * self.width + rect.x - GUTTER + x) * 4) as usize;
                let alpha = u16::from(source[at + 3]);
                for channel in 0..3 {
                    self.premultiplied_rgba[out + channel] =
                        ((u16::from(source[at + channel]) * alpha + 127) / 255) as u8;
                }
                self.premultiplied_rgba[out + 3] = source[at + 3];
            }
        }
    }

    pub fn uv(&self, rect: Rect, uv: [u8; 2]) -> [f32; 2] {
        // No added half texel: preserve original standalone sampler coordinates.
        [
            (rect.x as f32 + f32::from(uv[0]) / 256. * rect.width as f32) / self.width as f32,
            (rect.y as f32 + f32::from(uv[1]) / 256. * rect.height as f32) / self.height as f32,
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn zero_alpha_color_is_removed_and_edges_are_extruded() {
        let image = Image {
            width: 2,
            height: 1,
            flags: 6,
            rgba: vec![200, 100, 40, 128, 255, 0, 255, 0],
            consumed: 0,
        };
        let atlas = Atlas::pack(&[image]).unwrap();
        let rect = atlas.regions[0];
        let pixel = |x, y| {
            &atlas.premultiplied_rgba
                [((y * atlas.width + x) * 4) as usize..((y * atlas.width + x) * 4 + 4) as usize]
        };
        assert_eq!(pixel(rect.x, rect.y), [100, 50, 20, 128]);
        assert_eq!(pixel(rect.x + 1, rect.y), [0, 0, 0, 0]);
        assert_eq!(pixel(rect.x - 2, rect.y - 2), pixel(rect.x, rect.y));
        assert_eq!(pixel(rect.x + 3, rect.y + 2), pixel(rect.x + 1, rect.y));
    }
}
