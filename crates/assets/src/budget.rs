//! Decimal bytes, measured after decompression, including alignment/capacity.

pub const PREFERRED_MAP_BYTES: usize = 1_000_000;
pub const MAX_MAP_BYTES: usize = 2_000_000;

#[derive(Debug, Default)]
pub struct MapBudget {
    bytes: usize,
}

impl MapBudget {
    /// Account actual resident storage, not compressed file length.
    pub fn account(&mut self, bytes: usize) -> Result<(), &'static str> {
        let total = self
            .bytes
            .checked_add(bytes)
            .ok_or("map acceleration size overflow")?;
        if total > MAX_MAP_BYTES {
            return Err("map acceleration exceeds the 2 MB absolute limit");
        }
        self.bytes = total;
        Ok(())
    }

    pub fn bytes(&self) -> usize {
        self.bytes
    }

    pub fn needs_performance_evidence(&self) -> bool {
        self.bytes > PREFERRED_MAP_BYTES
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejected_allocation_does_not_change_accounting() {
        let mut budget = MapBudget::default();
        budget.account(PREFERRED_MAP_BYTES).unwrap();
        assert!(!budget.needs_performance_evidence());
        budget.account(1).unwrap();
        assert!(budget.needs_performance_evidence());
        assert!(budget.account(MAX_MAP_BYTES).is_err());
        assert_eq!(budget.bytes(), PREFERRED_MAP_BYTES + 1);
        assert!(budget.account(usize::MAX).is_err());
    }
}
