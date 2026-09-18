#include "World/AetherShellDefinition.h"

namespace
{
TArray<FAetherShellPiece> BuildPieces()
{
 TArray<FAetherShellPiece> P={
 {"Ground",{0,0,-400},{800,800,2},false},{"Town",{0,0,-60},{200,200,1.2},false},
 {"SouthRoad",{-6500,-19000,-50},{16,220,1},false},{"WestRoad",{-18000,0,-50},{180,16,1},false},
 {"EastRoad",{18000,0,-50},{180,16,1},false},{"NorthRoad",{0,18000,-50},{16,180,1},false},
 {"ForestFloor",{-27000,0,-60},{80,100,1.2},false},{"WorksWest",{26500,0,-60},{10,24,1.2},false},
 {"WorksEast",{28200,0,-60},{16,24,1.2},false},{"Maintenance",{27350,1000,-20},{9,3,.4},false},
 {"BridgeStopA",{26970,-600,-30},{.4,3,.6},false},{"BridgeStopB",{27730,-600,-30},{.4,3,.6},false},
 {"AbbeyFloor",{0,27000,-60},{70,80,1.2},false},{"ActivityFloor",{25000,22000,-60},{50,50,1.2},false},
 {"RingEast",{25000,12000,-50},{16,220,1},false},{"RingNorth",{12500,26000,-50},{250,16,1},false}};
 for(int I=0;I<6;++I)
 {const float X=(I%3-1)*1900.f,Y=I<3?-1800.f:1800.f;P.Add({*FString::Printf(TEXT("House%d"),I),{X,Y,220},{10,8,4.4},true});P.Add({*FString::Printf(TEXT("Roof%d"),I),{X,Y,480},{11,9,.8},true});}
 for(int I=0;I<8;++I)for(int Side:{-1,1})P.Add({*FString::Printf(TEXT("Pillar%d_%d"),I,Side),{Side*2200.f,24600.f+I*700,250},{1.2,1.2,5},true});
 return P;
}
}

const TArray<FAetherShellPiece>& AetherShell::Pieces()
{
    static const auto Data = BuildPieces();
    return Data;
}

bool AetherShell::IsShellPiece(FName Id)
{
    return Pieces().ContainsByPredicate([Id](const FAetherShellPiece& Piece) { return Piece.Id == Id; });
}
