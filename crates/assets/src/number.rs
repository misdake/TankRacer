// Old CRT decimal-prefix conversion, shared by world and class parameters.
pub(crate) fn decimal(text: &str) -> f64 {
    // strtod accepts a numeric prefix and D/d exponents in the source wrapper.
    let text = text.trim_start_matches(|c: char| c.is_ascii_whitespace());
    let digits = text.strip_prefix(['-', '+']).unwrap_or(text);
    let mut end = text.len() - digits.len();
    let bytes = text.as_bytes();
    while bytes.get(end).is_some_and(u8::is_ascii_digit) {
        end += 1;
    }
    if bytes.get(end) == Some(&b'.') {
        end += 1;
        while bytes.get(end).is_some_and(u8::is_ascii_digit) {
            end += 1;
        }
    }
    if bytes
        .get(end)
        .is_some_and(|b| matches!(b, b'e' | b'E' | b'd' | b'D'))
    {
        let exponent = end;
        end += 1;
        if bytes.get(end).is_some_and(|b| matches!(b, b'+' | b'-')) {
            end += 1;
        }
        let start = end;
        while bytes.get(end).is_some_and(u8::is_ascii_digit) {
            end += 1;
        }
        if start == end {
            end = exponent;
        }
    }
    let number = &text[..end];
    if number.contains(['d', 'D']) {
        number.replace(['d', 'D'], "e").parse::<f64>().unwrap_or(0.)
    } else {
        number.parse::<f64>().unwrap_or(0.)
    }
}
