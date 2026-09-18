#include "ReactiveTypes.h"

Reactive::FMaterial FReactiveMaterialParameters::ToCore() const
{
    Reactive::FMaterial M;
    M.DryMassKg = DryMassKg; M.SpecificHeatJPerKgK = SpecificHeatJPerKgK;
    M.WaterCapacityKg = WaterCapacityKg; M.InitialFuelKg = InitialFuelKg;
    M.IgnitionC = IgnitionC; M.BurnRateKgPerSec = BurnRateKgPerSec;
    M.CombustionJPerKg = CombustionJPerKg; M.RetainedHeatFraction = RetainedHeatFraction;
    M.bLiquidConductor = bLiquidConductor; M.Conductivity = Conductivity; M.ThermalCouplingWPerK = ThermalCouplingWPerK;
    M.CoolingWPerK = CoolingWPerK; M.StrengthNs = StrengthNs; M.CutResistanceJ = CutResistanceJ;
    M.FrozenStrengthMultiplier = FrozenStrengthMultiplier;
    M.SealedVolumeM3 = SealedVolumeM3; M.BurstGaugePressurePa = BurstGaugePressurePa;
    return M;
}
FReactiveSnapshot FReactiveSnapshot::FromCore(const Reactive::FState& S)
{
    FReactiveSnapshot R; R.TemperatureC = S.TemperatureC; R.WaterKg = S.WaterKg;
    R.ElectricalWaterKg=S.ElectricalWaterKg; R.ElectricalWetness01 = S.ElectricalWetness01; R.IceFraction = S.IceFraction; R.FuelKg = S.FuelKg; R.Integrity = S.Integrity;
    R.GaugePressurePa = S.GaugePressurePa; R.bBurning = S.bBurning;
    R.bBroken = S.bBroken; R.bBurst = S.bBurst; return R;
}
