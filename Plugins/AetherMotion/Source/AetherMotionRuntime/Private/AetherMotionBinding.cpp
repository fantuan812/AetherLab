#include "AetherMotionBinding.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
namespace
{
bool Fields(const FJsonObject& O,std::initializer_list<const TCHAR*> Expected)
{
 if(O.Values.Num()!=int32(Expected.size()))return false;
 for(const auto& Pair:O.Values){bool Found=false;for(const TCHAR* Key:Expected)Found|=Pair.Key.Equals(Key,ESearchCase::CaseSensitive);if(!Found)return false;}
 return true;
}
bool Text(const FJsonObject& O,const TCHAR* Key,FString& Value)
{return O.TryGetStringField(Key,Value)&&!Value.IsEmpty()&&Value.Len()<=512;}
bool Name(const FJsonObject& O,const TCHAR* Key,FName& Value)
{
 FString S;if(!Text(O,Key,S)||S.Len()>64||!FChar::IsAlpha(S[0]))return false;
 for(TCHAR C:S)if(!FChar::IsAlnum(C)&&C!='_')return false;Value=FName(S);return true;
}
bool Package(const FJsonObject& O,const TCHAR* Key,FString& Value)
{return Text(O,Key,Value)&&Value.StartsWith(TEXT("/Game/"))&&FPackageName::IsValidLongPackageName(Value);}
FSoftObjectPath ObjectPath(const FString& Package)
{return FSoftObjectPath(Package+TEXT(".")+FPackageName::GetLongPackageAssetName(Package));}
bool ClassPath(const FJsonObject& O,const TCHAR* Key,FString& Value)
{
 if(!Text(O,Key,Value))return false;const FSoftObjectPath P(Value);
 return P.IsValid()&&P.GetLongPackageName().StartsWith(TEXT("/Game/"))&&P.GetAssetName()==FPackageName::GetLongPackageAssetName(P.GetLongPackageName())+TEXT("_C");
}
bool Row(const FJsonObject& O,FAetherMotionBinding& B,FString& Why)
{
 if(!Fields(O,{TEXT("id"),TEXT("state"),TEXT("target_mesh"),TEXT("source_rig"),TEXT("target_rig"),TEXT("forward_retargeter"),TEXT("reverse_retargeter"),TEXT("profile"),TEXT("source_animation_class"),TEXT("source_root"),TEXT("target_pelvis"),TEXT("target_root"),TEXT("target_head"),TEXT("source_heading_degrees"),TEXT("chains"),TEXT("alignments"),TEXT("styles"),TEXT("character_definitions"),TEXT("animation_class"),TEXT("preview_idle"),TEXT("walk_animation"),TEXT("attack_animation"),TEXT("source_geometry"),TEXT("animations")}))return false;
 FName Id;FString State;
 if(!Name(O,TEXT("id"),Id)||!Text(O,TEXT("id"),B.Id)||!Text(O,TEXT("state"),State)||(!State.Equals(TEXT("draft"),ESearchCase::CaseSensitive)&&!State.Equals(TEXT("configured"),ESearchCase::CaseSensitive)))return false;
 B.Configured=State==TEXT("configured");
 if(!Package(O,TEXT("source_rig"),B.SourceRig)||!Package(O,TEXT("target_rig"),B.TargetRig)||!Package(O,TEXT("forward_retargeter"),B.Forward)||
    !Package(O,TEXT("reverse_retargeter"),B.Reverse)||!Package(O,TEXT("profile"),B.Profile)||!ClassPath(O,TEXT("source_animation_class"),B.SourceAnimationClass)||
    !ClassPath(O,TEXT("animation_class"),B.AnimationClass)||!Package(O,TEXT("preview_idle"),B.PreviewIdle)||!Package(O,TEXT("walk_animation"),B.WalkAnimation)||!Package(O,TEXT("attack_animation"),B.AttackAnimation)||
    !Name(O,TEXT("source_root"),B.SourceRoot)||!Name(O,TEXT("target_pelvis"),B.TargetPelvis)||!Name(O,TEXT("target_root"),B.TargetRoot)||!Name(O,TEXT("target_head"),B.TargetHead))return false;
 if(B.Configured)
 {if(!Package(O,TEXT("target_mesh"),B.TargetMesh)||!O.TryGetNumberField(TEXT("source_heading_degrees"),B.SourceHeading)||!FMath::IsFinite(B.SourceHeading)||FMath::Abs(B.SourceHeading)>180)return false;}
 else if(!O.HasTypedField<EJson::Null>(TEXT("target_mesh"))||!O.HasTypedField<EJson::Null>(TEXT("source_heading_degrees")))return false;
 const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;TSet<FName> Names;
 if(!O.TryGetArrayField(TEXT("chains"),Items)||Items->IsEmpty()||Items->Num()>64)return false;
 for(const auto& V:*Items){const auto C=V->AsObject();FAetherMotionBindingChain R;
  if(!C||!Fields(*C,{TEXT("name"),TEXT("source_start"),TEXT("source_end"),TEXT("target_start"),TEXT("target_end")})||
     !Name(*C,TEXT("name"),R.Name)||!Name(*C,TEXT("source_start"),R.SourceStart)||!Name(*C,TEXT("source_end"),R.SourceEnd)||
     !Name(*C,TEXT("target_start"),R.TargetStart)||!Name(*C,TEXT("target_end"),R.TargetEnd)||Names.Contains(R.Name))return false;
  Names.Add(R.Name);B.Chains.Add(R);}
 if(!O.TryGetArrayField(TEXT("alignments"),Items)||Items->Num()>32)return false;
 for(const auto& V:*Items){const auto C=V->AsObject();FAetherMotionBindingAlignment R;
  if(!C||!Fields(*C,{TEXT("source_bone"),TEXT("source_child"),TEXT("target_bone"),TEXT("target_child")})||
     !Name(*C,TEXT("source_bone"),R.SourceBone)||!Name(*C,TEXT("source_child"),R.SourceChild)||!Name(*C,TEXT("target_bone"),R.TargetBone)||!Name(*C,TEXT("target_child"),R.TargetChild)||R.SourceBone==R.SourceChild||R.TargetBone==R.TargetChild)return false;B.Alignments.Add(R);}
 const TSharedPtr<FJsonObject>* Obj=nullptr;
 if(!O.TryGetObjectField(TEXT("styles"),Obj)||(*Obj)->Values.Num()<2||(*Obj)->Values.Num()>32)return false;
 for(const auto& P:(*Obj)->Values){FString Value;if(!P.Value->TryGetString(Value)||Value.IsEmpty()||Value.Len()>64)return false;for(TCHAR C:Value)if(!FChar::IsAlnum(C)&&C!='_')return false;B.Styles.Add(FName(P.Key),Value);}
 if(!O.TryGetArrayField(TEXT("character_definitions"),Items)||Items->IsEmpty())return false;
 for(const auto& V:*Items){FString P;if(!V->TryGetString(P)||!P.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(P))return false;B.CharacterDefinitions.Add(P);}
 if(!O.TryGetObjectField(TEXT("animations"),Obj)||!Fields(**Obj,{TEXT("actions"),TEXT("locomotion"),TEXT("jump"),TEXT("fall"),TEXT("land"),TEXT("heavy"),TEXT("light")}))return false;
 if(B.Configured)
 {
  for(const TCHAR* Key:{TEXT("actions"),TEXT("locomotion"),TEXT("jump"),TEXT("fall"),TEXT("land"),TEXT("heavy")})
  {FString P;if(!Package(**Obj,Key,P))return false;B.AnimationAssets.Add(FName(Key),ObjectPath(P));}
  if(!(*Obj)->TryGetArrayField(TEXT("light"),Items)||Items->IsEmpty()||Items->Num()>16)return false;
  for(const auto& V:*Items){FString P;if(!V->TryGetString(P)||!P.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(P))return false;B.LightAnimations.Add(ObjectPath(P));}
 }
 else for(const auto& Pair:(*Obj)->Values)if(Pair.Value->Type!=EJson::Null)return false;
 return true;
}
}
FString AetherMotionBindings::DefinitionPath(){return FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/MotionBindings.json");}
bool AetherMotionBindings::Parse(const FString& Text,TArray<FAetherMotionBinding>& Out,FString& Why)
{
 Out.Reset();Why.Reset();TSharedPtr<FJsonObject> Root;double Version=0;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(Text.Len()>1024*1024||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||!Fields(*Root,{TEXT("schema"),TEXT("bindings")})||
    !Root->TryGetNumberField(TEXT("schema"),Version)||Version!=1||!Root->TryGetArrayField(TEXT("bindings"),Rows)||Rows->IsEmpty()||Rows->Num()>32)
 {Why=TEXT("Invalid MotionBindings schema/catalog");return false;}
 TSet<FName> IDs;TSet<FSoftObjectPath> Meshes,Outputs,SourceRigs,Characters;TArray<FAetherMotionBinding> Parsed;
 for(const auto& V:*Rows)
 {
  FAetherMotionBinding B;const auto O=V->AsObject();
  if(!O||!Row(*O,B,Why)){Why=TEXT("Invalid MotionBindings row: ")+B.Id;return false;}
  if(IDs.Contains(FName(B.Id))||(B.Configured&&Meshes.Contains(ObjectPath(B.TargetMesh)))){Why=TEXT("Duplicate rig ID or target mesh");return false;}
  IDs.Add(FName(B.Id));if(B.Configured)Meshes.Add(ObjectPath(B.TargetMesh));
  SourceRigs.Add(ObjectPath(B.SourceRig));
  for(const auto& P:{B.TargetRig,B.Forward,B.Reverse,B.Profile}){if(Outputs.Contains(ObjectPath(P))){Why=TEXT("Duplicate author output");return false;}Outputs.Add(ObjectPath(P));}
  for(const auto& P:B.CharacterDefinitions){if(Characters.Contains(ObjectPath(P))){Why=TEXT("Character has multiple bindings");return false;}Characters.Add(ObjectPath(P));}
  for(const auto& Prior:Parsed)if(ObjectPath(Prior.SourceRig)==ObjectPath(B.SourceRig))
  {
   if(Prior.SourceRoot!=B.SourceRoot||Prior.Chains.Num()!=B.Chains.Num()){Why=TEXT("Shared source rig contracts differ");return false;}
   for(int32 I=0;I<B.Chains.Num();++I)if(Prior.Chains[I].Name!=B.Chains[I].Name||Prior.Chains[I].SourceStart!=B.Chains[I].SourceStart||Prior.Chains[I].SourceEnd!=B.Chains[I].SourceEnd)
   {Why=TEXT("Shared source rig chains differ");return false;}
  }
  Parsed.Add(MoveTemp(B));
 }
 for(const auto& Source:SourceRigs)if(Outputs.Contains(Source)){Why=TEXT("Source rig aliases a managed target output");return false;}
 Out=MoveTemp(Parsed);return true;
}
bool AetherMotionBindings::Load(const FString& Path,TArray<FAetherMotionBinding>& Out,FString& Why)
{FString Text;if(!FFileHelper::LoadFileToString(Text,*Path)){Out.Reset();Why=TEXT("MotionBindings file unavailable: ")+Path;return false;}return Parse(Text,Out,Why);}
const TArray<FAetherMotionBinding>& AetherMotionBindings::All(FString& Why)
{
 struct FCatalog {TArray<FAetherMotionBinding> Rows;FString Error;FCatalog(){Load(DefinitionPath(),Rows,Error);}};
 static const FCatalog Catalog;Why=Catalog.Error;return Catalog.Rows;
}
const FAetherMotionBinding* AetherMotionBindings::ForMesh(const FSoftObjectPath& Mesh,FString& Why)
{
 for(const auto& B:All(Why))if(B.Configured&&ObjectPath(B.TargetMesh)==Mesh)return &B;
 if(Why.IsEmpty())Why=TEXT("No configured Motion binding for exact mesh path: ")+Mesh.ToString();return nullptr;
}
TArray<FSoftObjectPath> FAetherMotionBinding::RuntimeAssets() const
{
 TArray<FSoftObjectPath> Paths;if(!Configured)return Paths;
 for(const auto& P:{TargetMesh,Profile,Forward,PreviewIdle,WalkAnimation,AttackAnimation})Paths.AddUnique(ObjectPath(P));
 Paths.AddUnique(FSoftObjectPath(AnimationClass));Paths.AddUnique(FSoftObjectPath(SourceAnimationClass));
 for(const auto& Pair:AnimationAssets)Paths.AddUnique(Pair.Value);for(const auto& P:LightAnimations)Paths.AddUnique(P);
 return Paths;
}
