"""Targeted fault injection for unchanged A22. Synthetic outputs, never model scores.

Expands earlier happy-path tests to verify actual fallback and failure boundaries.
No GPU, network, shell launcher or installed third-party package is used.
"""
import argparse, contextlib, copy, hashlib, importlib.util, io, json, sys
from pathlib import Path
from unittest.mock import patch
sys.dont_write_bytecode = True

def load(p):
    spec=importlib.util.spec_from_file_location('a22_boundary',p)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    a.output.mkdir(parents=True,exist_ok=False); ri=load(a.project/'run_inference.py')
    base={'response_text':'SYNTHETIC_REPLY','emotion_label':'anxiety','user_profile':{'personality_traits':['sensitive'],'interests':['music'],'style':['brief']},'memory_refs':[]}
    history=[{'role':'user','content':'我喜欢阅读科幻小说。'},{'role':'assistant','content':'你最近读什么？'},{'role':'user','content':'我喜欢阅读科幻小说，今天很开心。'}]
    checks=[]
    def scenario(name,change=None,fatal=False,no_retrieval=False):
        out=a.output/name;out.mkdir();(out/'keep.txt').write_text('KEEP',encoding='utf-8')
        inp=out/'input.jsonl';sample={'id':'test_999999_t3','history':history if not no_retrieval else history[-1:]}
        inp.write_text(json.dumps(sample,ensure_ascii=False)+'\n',encoding='utf-8')
        replies={'main':copy.deepcopy(base),'emotion':{'emotion_label':'joy'},'profile':{'user_profile':{'personality_traits':[],'interests':['reading_writing'],'style':[]}},'gate':{'retain_sensitive':False}}
        if change: replies.update(change)
        calls=[]
        def mock(config,messages,options):
            prompt=messages[0]['content']
            key='emotion' if '只判断最新用户消息此刻' in prompt else 'profile' if '只判断用户长期画像' in prompt else 'gate' if 'retain_sensitive' in prompt else 'main'
            calls.append({'channel':key,'messages':messages,'options':options})
            value=replies[key]
            if isinstance(value,Exception): raise value
            return value if isinstance(value,str) else json.dumps(value,ensure_ascii=False)
        buf=io.StringIO();error=None
        with patch.object(ri,'request_chat_completion',mock),patch.object(sys,'argv',['run_inference.py',str(inp),str(out)]),contextlib.redirect_stdout(buf):
            try:ri.main()
            except RuntimeError as exc:error=str(exc)
        (out/'console.log').write_text(buf.getvalue(),encoding='utf-8')
        assert bool(error)==fatal,(name,error)
        assert (out/'keep.txt').read_text()=='KEEP'
        rec={'case':name,'expected_batch_abort':fatal,'observed_error':error,'calls':[x['channel'] for x in calls],'passed':True}
        if not fatal:
            pred=json.loads((out/'submission.jsonl').read_text(encoding='utf-8'))
            audit=json.loads((out/'a22_channel_audit.jsonl').read_text(encoding='utf-8'))
            gate=json.loads((out/'sensitive_gate_audit.jsonl').read_text(encoding='utf-8'))
            report=json.loads((out/'performance_report.json').read_text(encoding='utf-8'))
            assert set(pred)=={'id','response_text','emotion_label','user_profile','memory_refs'}
            assert pred['id']==sample['id']
            assert report['samples']==report['rounds']==1 and report['complete'] is False
            assert report['submission_sha256']==hashlib.sha256((out/'submission.jsonl').read_bytes()).hexdigest()
            rec.update(prediction=pred,audit=audit,gate=gate)
        checks.append(rec);return rec,calls
    r,c=scenario('normal_four_channels')
    assert r['calls']==['main','emotion','profile','gate']
    assert r['prediction']['response_text']=='SYNTHETIC_REPLY' and r['prediction']['emotion_label']=='joy'
    assert r['prediction']['user_profile']=={'personality_traits':[],'interests':['reading_writing'],'style':['brief']}
    assert '相关历史片段' not in c[0]['messages'][1]['content']
    assert 'personality_traits:' not in c[2]['messages'][0]['content'] # known missing enum contract
    r,_=scenario('no_retrieval',no_retrieval=True);assert r['calls']==['main','emotion','gate']
    for name,value in [('emotion_transport_error',RuntimeError('INJECTED')),('emotion_invalid_json','BROKEN'),('emotion_illegal_enum',{'emotion_label':'UNKNOWN'})]:
        r,_=scenario(name,{'emotion':value});assert r['prediction']['emotion_label']=='anxiety' and r['audit']['emotion_review_failed']
    r,_=scenario('profile_transport_error',{'profile':RuntimeError('INJECTED')});assert r['prediction']['user_profile']['interests']==['music'] and r['audit']['profile_failed']
    r,_=scenario('profile_empty_does_not_erase',{'profile':{'user_profile':{}}});assert r['prediction']['user_profile']['interests']==['music'] and not r['audit']['profile_failed']
    r,_=scenario('profile_invalid_dimension_silent',{'profile':{'user_profile':{'interests':['music','NOT_ENUM']}}});assert r['prediction']['user_profile']['interests']==['music'] and not r['audit']['profile_failed']
    r,_=scenario('gate_invalid_retains',{'gate':'BROKEN'});assert 'sensitive' in r['prediction']['user_profile']['personality_traits'] and r['gate']['gate_parse_failed']
    scenario('gate_transport_aborts',{'gate':RuntimeError('INJECTED_GATE')},fatal=True)
    scenario('main_transport_aborts',{'main':RuntimeError('INJECTED_MAIN')},fatal=True)
    r,_=scenario('main_bad_json_emotion_recovered',{'main':'{"response_text":"BROKEN_REPLY"'});assert r['prediction']['emotion_label']=='joy' and r['gate']['main_parse_failed']
    # Official normalizer: a single illegal/duplicate label drops the whole dimension.
    for arr in [['music','music'],['music','UNKNOWN']]:
        assert ri.normalize_prediction('test_999999_t1',{'user_profile':{'interests':arr}})['user_profile']['interests']==[]
    result={'kind':'SYNTHETIC_BOUNDARY_TEST_NOT_MODEL','model_inference':False,'gpu_test':False,'start_sh_test':False,'source_sha256':hashlib.sha256((a.project/'run_inference.py').read_bytes()).hexdigest(),'case_count':len(checks),'all_passed':True,'checks':checks}
    (a.output/'boundary_results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='checks'},ensure_ascii=False))

if __name__=='__main__':main()
