//! Weights as the native shell's config formatter writes them: `%f` from
//! its stb_sprintf (native/src/third_party/stb/stb_sprintf.h), read back by
//! its `f64_from_str8`. An edit stores each weight it writes this way, so a
//! committed document, read again, lays out as the edited one did.
//!
//! stb_sprintf's `%f` is not the C library's: it reaches 18 or 19
//! significant digits with double-double arithmetic and then rounds half up
//! in integers, where a correctly rounded `%f` rounds ties to even (1/128 is
//! 0.007813 here, 0.007812 there). This is that algorithm, step for step.

const BOT: [f64; 23] = [
    1e+000, 1e+001, 1e+002, 1e+003, 1e+004, 1e+005, 1e+006, 1e+007, 1e+008, 1e+009, 1e+010, 1e+011, 1e+012, 1e+013, 1e+014, 1e+015, 1e+016,
    1e+017, 1e+018, 1e+019, 1e+020, 1e+021, 1e+022,
];
const NEGBOT: [f64; 22] = [
    1e-001, 1e-002, 1e-003, 1e-004, 1e-005, 1e-006, 1e-007, 1e-008, 1e-009, 1e-010, 1e-011, 1e-012, 1e-013, 1e-014, 1e-015, 1e-016, 1e-017,
    1e-018, 1e-019, 1e-020, 1e-021, 1e-022,
];
#[allow(clippy::excessive_precision)]
const NEGBOTERR: [f64; 22] = [
    -5.551115123125783e-018,
    -2.0816681711721684e-019,
    -2.0816681711721686e-020,
    -4.7921736023859299e-021,
    -8.1803053914031305e-022,
    4.5251888174113741e-023,
    4.5251888174113739e-024,
    -2.0922560830128471e-025,
    -6.2281591457779853e-026,
    -3.6432197315497743e-027,
    6.0503030718060191e-028,
    2.0113352370744385e-029,
    -3.0373745563400371e-030,
    1.1806906454401013e-032,
    -7.7705399876661076e-032,
    2.0902213275965398e-033,
    -7.1542424054621921e-034,
    -7.1542424054621926e-035,
    2.4754073164739869e-036,
    5.4846728545790429e-037,
    9.2462547772103625e-038,
    -4.8596774326570872e-039,
];
const TOP: [f64; 13] = [1e+023, 1e+046, 1e+069, 1e+092, 1e+115, 1e+138, 1e+161, 1e+184, 1e+207, 1e+230, 1e+253, 1e+276, 1e+299];
const NEGTOP: [f64; 13] = [1e-023, 1e-046, 1e-069, 1e-092, 1e-115, 1e-138, 1e-161, 1e-184, 1e-207, 1e-230, 1e-253, 1e-276, 1e-299];
#[allow(clippy::excessive_precision)]
const TOPERR: [f64; 13] = [
    8388608.0,
    6.8601809640529717e+028,
    -7.253143638152921e+052,
    -4.3377296974619174e+075,
    -1.5559416129466825e+098,
    -3.2841562489204913e+121,
    -3.7745893248228135e+144,
    -1.7356668416969134e+167,
    -3.8893577551088374e+190,
    -9.9566444326005119e+213,
    6.3641293062232429e+236,
    -5.2069140800249813e+259,
    -5.2504760255204387e+282,
];
#[allow(clippy::excessive_precision)]
const NEGTOPERR: [f64; 13] = [
    3.9565301985100693e-040,
    -2.299904345391321e-063,
    3.6506201437945798e-086,
    1.1875228833981544e-109,
    -5.0644902316928607e-132,
    -6.7156837247865426e-155,
    -2.812077463003139e-178,
    -5.7778912386589953e-201,
    7.4997100559334532e-224,
    -4.6439668915134491e-247,
    -6.3691100762962136e-270,
    -9.436808465446358e-293,
    8.0970921678014997e-317,
];
const POWTEN: [u64; 20] = [
    1,
    10,
    100,
    1_000,
    10_000,
    100_000,
    1_000_000,
    10_000_000,
    100_000_000,
    1_000_000_000,
    10_000_000_000,
    100_000_000_000,
    1_000_000_000_000,
    10_000_000_000_000,
    100_000_000_000_000,
    1_000_000_000_000_000,
    10_000_000_000_000_000,
    100_000_000_000_000_000,
    1_000_000_000_000_000_000,
    10_000_000_000_000_000_000,
];

/// The top 26 bits of the significand, for an exact product.
fn high(x: f64) -> f64 {
    f64::from_bits(x.to_bits() & (!0u64 << 27))
}

fn ddmulthi(xh: f64, yh: f64) -> (f64, f64) {
    let oh = xh * yh;
    let (ahi, bhi) = (high(xh), high(yh));
    let (alo, blo) = (xh - ahi, yh - bhi);
    (oh, ((ahi * bhi - oh) + ahi * blo + alo * bhi) + alo * blo)
}

fn ddrenorm(oh: f64, ol: f64) -> (f64, f64) {
    let s = oh + ol;
    (s, ol - (s - oh))
}

/// C's double-to-int64 conversion as x86-64 makes it: out of range is
/// INT64_MIN. stb only reaches it for magnitudes near 1e19.
fn to_i64(x: f64) -> i64 {
    if x >= -9.223372036854775808e18 && x < 9.223372036854775808e18 {
        x as i64
    } else {
        i64::MIN
    }
}

fn ddto_s64(xh: f64, xl: f64) -> i64 {
    let ob = to_i64(xh);
    let vh = ob as f64;
    let ahi = xh - vh;
    let t = ahi - xh;
    let alo = (xh - (ahi - t)) - (vh + t);
    ob.wrapping_add(to_i64(ahi + alo + xl))
}

fn raise_to_power10(d: f64, power: i32) -> (f64, f64) {
    let (mut ph, mut pl);
    if (0..=22).contains(&power) {
        (ph, pl) = ddmulthi(d, BOT[power as usize]);
    } else {
        let mut e = power.abs();
        let mut et = ((e * 0x2c9) >> 14).min(13);
        let mut eb = e - et * 23;
        ph = d;
        pl = 0.0;
        if power < 0 {
            if eb != 0 {
                eb -= 1;
                (ph, pl) = ddmulthi(d, NEGBOT[eb as usize]);
                pl += d * NEGBOTERR[eb as usize];
            }
            if et != 0 {
                (ph, pl) = ddrenorm(ph, pl);
                et -= 1;
                let (p2h, p2l) = ddmulthi(ph, NEGTOP[et as usize]);
                (ph, pl) = (p2h, p2l + (ph * NEGTOPERR[et as usize] + pl * NEGTOP[et as usize]));
            }
        } else {
            if eb != 0 {
                e = eb;
                eb = eb.min(22);
                e -= eb;
                (ph, pl) = ddmulthi(d, BOT[eb as usize]);
                if e != 0 {
                    (ph, pl) = ddrenorm(ph, pl);
                    let (p2h, p2l) = ddmulthi(ph, BOT[e as usize]);
                    (ph, pl) = (p2h, p2l + BOT[e as usize] * pl);
                }
            }
            if et != 0 {
                (ph, pl) = ddrenorm(ph, pl);
                et -= 1;
                let (p2h, p2l) = ddmulthi(ph, TOP[et as usize]);
                (ph, pl) = (p2h, p2l + (ph * TOPERR[et as usize] + pl * TOP[et as usize]));
            }
        }
    }
    ddrenorm(ph, pl)
}

enum Repr {
    Special(&'static str),
    /// The value is 0.`digits` times ten to the `dp`.
    Digits {
        digits: String,
        dp: i32,
    },
}

/// stbsp__real_to_str, rounding at `frac_digits` after the point.
fn real_to_str(value: f64, frac_digits: u32) -> (bool, Repr) {
    let raw = value.to_bits();
    let mut expo = ((raw >> 52) & 2047) as i32;
    let negative = raw >> 63 != 0;
    let d = if negative { -value } else { value };
    if expo == 2047 {
        return (negative, Repr::Special(if raw & ((1 << 52) - 1) != 0 { "NaN" } else { "Inf" }));
    }
    if expo == 0 {
        if raw << 1 == 0 {
            return (negative, Repr::Digits { digits: "0".into(), dp: 1 });
        }
        let mut v: u64 = 1 << 51;
        while raw & v == 0 {
            expo -= 1;
            v >>= 1;
        }
    }
    let mut tens = expo - 1023;
    tens = if tens < 0 { (tens * 617) / 2048 } else { (tens * 1233) / 4096 + 1 };
    let (ph, pl) = raise_to_power10(d, 18 - tens);
    let mut bits = ddto_s64(ph, pl) as u64;
    if bits >= POWTEN[18] {
        tens += 1;
    }

    // Round in integers. A negative `tens` wraps, as C's unsigned sum does,
    // and skips the rounding.
    let frac_digits = (tens as u32).wrapping_add(frac_digits);
    if frac_digits < 24 {
        let mut dg: u32 = 1;
        if bits >= POWTEN[9] {
            dg = 10;
        }
        let mut round = true;
        while bits >= POWTEN[dg as usize] {
            dg += 1;
            if dg == 20 {
                round = false;
                break;
            }
        }
        if round && frac_digits < dg {
            let e = dg - frac_digits;
            if e < 24 {
                let r = POWTEN[e as usize];
                bits = bits.wrapping_add(r / 2);
                if bits >= POWTEN[dg as usize] {
                    tens += 1;
                }
                bits /= r;
            }
        }
    }

    // Trailing zeros go; the C does this in signed arithmetic.
    let mut signed = bits as i64;
    if signed != 0 {
        let mut whole = false;
        loop {
            if signed <= 0xffff_ffff {
                whole = true;
                break;
            }
            if signed % 1000 != 0 {
                break;
            }
            signed /= 1000;
        }
        if whole {
            let mut n = signed as u32;
            while n != 0 && n % 1000 == 0 {
                n /= 1000;
            }
            signed = n as i64;
        }
    }
    let digits = if signed == 0 { String::new() } else { (signed as u64).to_string() };
    (negative, Repr::Digits { digits, dp: tens })
}

/// `value` as stb_sprintf's `%f` writes it (six places).
pub(crate) fn format(value: f64) -> String {
    const PR: usize = 6;
    let (negative, repr) = real_to_str(value, PR as u32);
    let mut s = String::new();
    if negative {
        s.push('-');
    }
    match repr {
        Repr::Special(text) => s.push_str(text),
        Repr::Digits { digits, dp } => {
            let l = digits.len();
            if dp <= 0 {
                s.push_str("0.");
                let n = ((-dp) as usize).min(PR);
                s.extend(std::iter::repeat_n('0', n));
                let l = if l + n > PR { PR - n } else { l };
                s.push_str(&digits[..l]);
                s.extend(std::iter::repeat_n('0', PR - (n + l)));
            } else if dp as usize >= l {
                s.push_str(&digits);
                s.extend(std::iter::repeat_n('0', dp as usize - l));
                s.push('.');
                s.extend(std::iter::repeat_n('0', PR));
            } else {
                let dp = dp as usize;
                s.push_str(&digits[..dp]);
                s.push('.');
                let l = if l - dp > PR { PR + dp } else { l };
                s.push_str(&digits[dp..l]);
                s.extend(std::iter::repeat_n('0', PR - (l - dp)));
            }
        }
    }
    s
}

/// The native `f64_from_str8`: a sign, then the number's characters, at
/// most 63, through a correctly rounded `atof`.
pub(crate) fn parse(text: &str) -> f64 {
    let bytes = text.as_bytes();
    let sign = if bytes.first() == Some(&b'-') { -1.0 } else { 1.0 };
    let mut buffer = String::new();
    let mut exponent = false;
    for &c in bytes {
        if buffer.len() >= 63 {
            break;
        }
        if c.is_ascii_digit() || c == b'.' || c == b'e' || (exponent && (c == b'+' || c == b'-')) {
            buffer.push(c as char);
            exponent = c == b'e';
        }
    }
    sign * atof(&buffer)
}

/// C's `atof` on what `parse` gathered: the longest prefix that is a number.
fn atof(text: &str) -> f64 {
    (0..=text.len()).rev().find_map(|end| text[..end].parse::<f64>().ok().filter(|_| !text[..end].is_empty())).unwrap_or(0.0)
}

/// A weight as an edit stores it (`rd_arrangement_quantize`).
pub fn quantize(weight: f32) -> f32 {
    parse(&format(weight as f64)) as f32
}
