"""作者与运行时共享同一 MotionBindings 目录；本模块负责作者侧校验与资产写入。"""
import json
import math
import re

_FIELDS = {'id','state','target_mesh','source_rig','target_rig','forward_retargeter','reverse_retargeter',
           'profile','source_animation_class','source_root','target_pelvis','target_root','target_head',
           'source_heading_degrees','chains','alignments','styles','character_definitions',
           'animation_class','preview_idle','walk_animation','attack_animation','source_geometry','animations'}
_PACKAGE = re.compile(r'^/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+$')
_BONE = re.compile(r'^[A-Za-z][A-Za-z0-9_]{0,63}$')

def _require(ok, message):
    if not ok:
        raise ValueError(message)


def load_bindings(path):
    def pairs(items):
        result = {}
        for key, value in items:
            _require(key not in result, '重复 JSON 字段：' + key)
            result[key] = value
        return result
    data = json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=pairs)
    _require(set(data) == {'schema', 'bindings'} and type(data['schema']) is int and data['schema'] == 1,
             '不支持的 Motion binding schema')
    _require(type(data['bindings']) is list and 0 < len(data['bindings']) <= 32, '骨架目录数量无效')
    result = {}
    outputs, characters, meshes = set(), set(), set()
    source_contracts = {}
    for row in data['bindings']:
        _require(type(row) is dict and set(row) == _FIELDS, '骨架定义字段缺失、未知或大小写错误')
        identity = row['id']
        _require(type(identity) is str and _BONE.fullmatch(identity) and identity.casefold() not in result,
                 '骨架 ID 无效或重复')
        _require(row['state'] in ('configured', 'draft'), '无效作者状态')
        for key in ('source_rig','target_rig','forward_retargeter','reverse_retargeter','profile','preview_idle','walk_animation','attack_animation'):
            _require(type(row[key]) is str and _PACKAGE.fullmatch(row[key]), '无效资源包：' + key)
        for key in ('target_rig','forward_retargeter','reverse_retargeter','profile'):
            _require(row[key].casefold() not in outputs, '重复作者输出：' + row[key]); outputs.add(row[key].casefold())
        for key in ('source_animation_class','animation_class'):
            value = row[key]
            _require(type(value) is str and '.' in value and _PACKAGE.fullmatch(value.split('.')[0])
                     and value.split('.')[1] == value.split('/')[-1].split('.')[0] + '_C', '无效动画类路径：' + key)
        for key in ('source_root','target_pelvis','target_root','target_head'):
            _require(type(row[key]) is str and _BONE.fullmatch(row[key]), '无效骨名：' + key)
        if row['state'] == 'configured':
            _require(type(row['target_mesh']) is str and _PACKAGE.fullmatch(row['target_mesh']), '缺少明确目标网格')
            _require(row['target_mesh'].casefold() not in meshes, '目标网格绑定重复')
            meshes.add(row['target_mesh'].casefold())
            angle = row['source_heading_degrees']
            _require(type(angle) in (int,float) and math.isfinite(angle) and -180 <= angle <= 180, '未配置导入后朝向')
        else:
            _require(row['target_mesh'] is None and row['source_heading_degrees'] is None,
                     '草案不得填入未经确认的目标网格/朝向')
        animation = row['animations']
        _require(type(animation) is dict and set(animation) == {'actions','locomotion','jump','fall','land','heavy','light'}, '动画绑定字段无效')
        if row['state'] == 'configured':
            _require(all(type(animation[k]) is str and _PACKAGE.fullmatch(animation[k]) for k in animation if k != 'light'), '动画资源路径无效')
            _require(type(animation['light']) is list and 0 < len(animation['light']) <= 16 and all(type(p) is str and _PACKAGE.fullmatch(p) for p in animation['light']), '轻击资源无效')
        else:
            _require(all(v is None for v in animation.values()), '草案不能伪造动画绑定已完成')
        chain_names = set()
        _require(type(row['chains']) is list and 0 < len(row['chains']) <= 64, '缺少骨链')
        for chain in row['chains']:
            _require(set(chain) == {'name','source_start','source_end','target_start','target_end'}
                     and all(type(v) is str and _BONE.fullmatch(v) for v in chain.values()), '无效骨链')
            _require(chain['name'].casefold() not in chain_names, '重复骨链')
            chain_names.add(chain['name'].casefold())
        _require(type(row['alignments']) is list and len(row['alignments']) <= 32, '轴对齐条目无效')
        for pair in row['alignments']:
            _require(set(pair) == {'source_bone','source_child','target_bone','target_child'}
                     and all(type(v) is str and _BONE.fullmatch(v) for v in pair.values()), '无效轴对齐')
            _require(pair['source_bone'] != pair['source_child'] and pair['target_bone'] != pair['target_child'], '零长度轴定义')
        _require(type(row['styles']) is dict and 2 <= len(row['styles']) <= 32 and
                 all(_BONE.fullmatch(k) and type(v) is str and _BONE.fullmatch(v) for k,v in row['styles'].items()), '风格字典无效')
        _require(type(row['character_definitions']) is list and bool(row['character_definitions']), '未指定角色定义')
        for asset in row['character_definitions']:
            _require(type(asset) is str and _PACKAGE.fullmatch(asset) and asset.casefold() not in characters, '角色不能有两套骨架绑定')
            characters.add(asset.casefold())
        contract = (row['source_root'], [(c['name'],c['source_start'],c['source_end']) for c in row['chains']])
        _require(row['source_rig'].casefold() not in source_contracts or source_contracts[row['source_rig'].casefold()] == contract,
                 '共享 G1 Rig 的骨链不一致')
        source_contracts[row['source_rig'].casefold()] = contract
        result[identity.casefold()] = row
    _require(not (set(source_contracts) & outputs), '共享源 Rig 不得与任何目标作者输出重合')
    return {row['id']: row for row in result.values()}


def configured_binding(bindings, identity):
    _require(identity in bindings, '未声明骨架 ID：' + identity)
    row = bindings[identity]
    _require(row['state'] == 'configured', '骨架仍为作者草案，目标网格/导入后朝向与原生资产未就绪：' + identity)
    return row


def apply_character_binding(ue, definition, row):
    """同一作者行同时定义身体、动画资源和 MotionProfile；不按网格名称回推。"""
    configured_binding({row['id']:row}, row['id'])
    library = ue.EditorAssetLibrary
    for field, key in [('body_mesh','target_mesh'),('preview_idle_animation','preview_idle'),
                       ('walk_animation','walk_animation'),('attack_animation','attack_animation')]:
        value = library.load_asset(row[key])
        if not value:
            raise RuntimeError('角色绑定资源缺失：' + row[key])
        definition.set_editor_property(field, value)
    animation = ue.load_class(None, row['animation_class'])
    if not animation:
        raise RuntimeError('角色动画类缺失：' + row['animation_class'])
    definition.set_editor_property('animation_class', animation)
