//! Logical class/interface slots. Method IDs are Rust bindings, never code addresses.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ClassId(pub usize);
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MethodId(pub u16);
#[derive(Debug, Clone)]
pub struct Class {
    pub maximum_slot: i32,
    pub methods: Vec<Option<MethodId>>,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    MissingSlot(u32),
    Method(i32),
}

/// Preserve the source dispatch's exceptional slot lifetime: an out-of-range
/// interface or a failing method leaves the current slot changed. Success,
/// including a null method, restores the saved slot. Result writes survive error.
pub fn call(
    class: &Class,
    slot: u32,
    current: &mut u32,
    result: &mut u32,
    invoke: impl FnOnce(MethodId, &mut u32, &mut u32) -> Result<(), i32>,
) -> Result<(), Error> {
    let old = *current;
    *current = slot;
    *result = 0;
    if slot as i32 > class.maximum_slot {
        return Ok(());
    }
    let method = class
        .methods
        .get(slot as usize)
        .ok_or(Error::MissingSlot(slot))?;
    if let Some(method) = method {
        invoke(*method, current, result)
            .map_err(|code| Error::Method(if code < 0 { code } else { -6 }))?;
    }
    *current = old;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn out_of_range_and_failed_methods_keep_their_slot_and_result_prefix() {
        let class = Class {
            maximum_slot: 0,
            methods: vec![Some(MethodId(4))],
        };
        let (mut slot, mut result) = (9, 55);
        assert_eq!(
            call(&class, 1, &mut slot, &mut result, |_, _, _| panic!(
                "out of range"
            )),
            Ok(())
        );
        assert_eq!((slot, result), (1, 0));
        assert_eq!(
            call(&class, 0, &mut slot, &mut result, |_, slot, result| {
                *slot = 7;
                *result = 13;
                Err(1)
            }),
            Err(Error::Method(-6))
        );
        assert_eq!((slot, result), (7, 13));
        assert_eq!(
            call(&class, 0, &mut slot, &mut result, |_, slot, result| {
                *slot = 3;
                *result = 17;
                Ok(())
            }),
            Ok(())
        );
        assert_eq!((slot, result), (7, 17));
    }
}
