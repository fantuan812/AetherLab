"""静态目录/受管资源/原骨合同检查，不导入 Unreal，不执行模型或游戏测试。"""
import argparse
import json
import pathlib
import sys
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts/Authoring'))
from MotionBindings import load_bindings


def inspect(native_contract=None):
    bindings = load_bindings(ROOT / 'Content/AetherCore/Definitions/MotionBindings.json')
    count = 0
    for row in bindings.values():
        if row['state'] != 'configured':
            continue
        paths = [row[k] for k in ('target_mesh','source_rig','target_rig','forward_retargeter','reverse_retargeter','profile','preview_idle','walk_animation','attack_animation')]
        paths += [row[k].split('.')[0] for k in ('animation_class','source_animation_class')]
        paths += [v for k,v in row['animations'].items() if k != 'light'] + row['animations']['light']
        paths += row['character_definitions']
        for path in paths:
            file = ROOT / 'Content' / (path.removeprefix('/Game/') + '.uasset')
            if not file.is_file():
                raise ValueError('绑定包文件不存在：' + str(file))
            count += 1
    source = json.loads((ROOT / 'ContentSource/Motion/G1Skeleton.json').read_text(encoding='utf-8-sig'))
    joints = source['joints']
    if len(joints) != 34:
        raise ValueError('锁定 G1 骨数改变')
    names = {j['name']:None if j['parent'] < 0 else joints[j['parent']]['name'] for j in joints}
    def chain(tree, start, end):
        if start not in tree or end not in tree:
            raise ValueError('缺失骨：' + start + '/' + end)
        current = end
        while current is not None:
            if current == start:
                return
            current = tree[current]
        raise ValueError('端骨不在起骨后代链：' + start + '/' + end)
    for row in bindings.values():
        for c in row['chains']:
            chain(names, c['source_start'], c['source_end'])
    native_checked = False
    if native_contract:
        contract = json.loads(pathlib.Path(native_contract).read_text())
        bones = contract['original_rest']['bones']
        if contract['native_bone_count'] != 65 or len(bones) != 65 or contract['units'] != 'meters':
            raise ValueError('不是原65骨米制合同')
        tree = {name:value['parent'] for name,value in bones.items()}
        row = bindings['Quaternius65']
        for key in ('target_root','target_pelvis','target_head'):
            if row[key] not in tree:
                raise ValueError('原骨缺失：' + row[key])
        for c in row['chains']:
            chain(tree, c['target_start'], c['target_end'])
        for a in row['alignments']:
            chain(tree, a['target_bone'], a['target_child'])
        native_checked = True
    return {'configured': [b['id'] for b in bindings.values() if b['state']=='configured'],
            'draft': [b['id'] for b in bindings.values() if b['state']=='draft'],
            'package_references_present':count,'g1_bones':34,'native65_contract_checked':native_checked,
            'ue_assets_loaded':False}


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--native-contract')
    args=parser.parse_args()
    print(json.dumps(inspect(args.native_contract),ensure_ascii=False,indent=2))
