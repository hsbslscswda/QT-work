"""Validate the exact frozen output contract, IDs and performance hash binding.

Standard library only. This is a checker for this package's simple schema,
not a general JSON Schema implementation. A dev/val ID exception must be explicit.
"""
import argparse, hashlib, json, math, re, statistics
from pathlib import Path

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def rows(p):return [json.loads(s) for s in p.read_text(encoding='utf-8-sig').splitlines() if s.strip()]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def schema_check(value,schema,path='',allow_dev=False):
    errors=[];kind=schema.get('type')
    if kind and not isinstance(value,{'object':dict,'array':list,'string':str}[kind]):return [path+': wrong type']
    if 'enum' in schema and value not in schema['enum']:errors.append(path+': outside enum')
    if 'pattern' in schema and isinstance(value,str) and not re.search(schema['pattern'],value):
        if not (allow_dev and path=='id' and re.fullmatch(r'(dev|val)_[0-9]{6}_t[0-9]+',value)):errors.append(path+': pattern')
    if kind=='object':
        for key in schema.get('required',[]):
            if key not in value:errors.append(path+'/'+key+': missing')
        if schema.get('additionalProperties') is False:
            errors += [path+'/'+key+': extra' for key in value if key not in schema['properties']]
        for key,item in value.items():
            if key in schema.get('properties',{}):errors+=schema_check(item,schema['properties'][key],(path+'/'+key).strip('/'),allow_dev)
    if kind=='array':
        if schema.get('uniqueItems') and len({json.dumps(v,sort_keys=True) for v in value})!=len(value):errors.append(path+': duplicate items')
        for index,item in enumerate(value):errors+=schema_check(item,schema['items'],path+f'/{index}',allow_dev)
    return errors
def validate(inp,pred,perf,schema,allow_dev=False):
    x=rows(inp);p=rows(pred);r=read(perf);sc=read(schema);errors=[]
    if not x:errors.append('empty input')
    ids=[v['id'] for v in x];pids=[v.get('id') for v in p]
    if len(ids)!=len(set(ids)):errors.append('duplicate input IDs')
    if len(pids)!=len(set(pids)):errors.append('duplicate output IDs')
    if pids!=ids:errors.append('output IDs/order differ from input')
    for i,v in enumerate(p):errors += [f'row {i+1}: {e}' for e in schema_check(v,sc,allow_dev=allow_dev)]
    for field,path in [('test_file_sha256',inp),('submission_sha256',pred)]:
        if r.get(field)!=sha(path):errors.append(field+' mismatch')
    required=r.get('required_rounds');lat=r.get('latencies_ms',[])
    if required!=100:errors.append('competition timing requires 100 rounds')
    expected=min(len(x),100)
    if r.get('samples')!=len(x) or r.get('rounds')!=expected or len(lat)!=expected:errors.append('sample/timing counts mismatch')
    if r.get('complete')!=(len(x)>=100):errors.append('complete flag mismatch')
    if not lat or any(not isinstance(v,(int,float)) or isinstance(v,bool) or not math.isfinite(v) or v<0 for v in lat):errors.append('invalid latency values')
    else:
        vals={'average_latency_ms':statistics.fmean(lat),'median_latency_ms':statistics.median(lat),'p95_latency_ms':sorted(lat)[math.ceil(.95*len(lat))-1],'min_latency_ms':min(lat),'max_latency_ms':max(lat)}
        for k,v in vals.items():
            if not isinstance(r.get(k),(int,float)) or abs(r[k]-v)>1.1e-6:errors.append(k+' inconsistent')
    return {'passed':not errors,'errors':errors,'samples':len(p),'development_ids_allowed':allow_dev,'empty_emotions':sum(not v.get('emotion_label') for v in p),'empty_responses':sum(not v.get('response_text','').strip() for v in p),'model_quality_evaluated':False}
def main():
    ap=argparse.ArgumentParser()
    for n in ['input','submission','performance','schema','output']:ap.add_argument('--'+n,type=Path,required=True)
    ap.add_argument('--allow-development-ids',action='store_true');a=ap.parse_args()
    if a.output.exists():raise SystemExit('Refusing to overwrite validation evidence')
    result=validate(a.input,a.submission,a.performance,a.schema,a.allow_development_ids)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,ensure_ascii=False))
    raise SystemExit(0 if result['passed'] else 1)
if __name__=='__main__':main()
