"""Qualify immutable mix diagnostics through the ordinary official MCP SDK on a private endpoint."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_control_test import ROOT, fixture_command, wire

async def check(fixture, output):
    """Verify read-only admission, schemas, exact guards, immutable pages and bounded retention."""
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sonics-mix-mcp-") as directory:
        root=Path(directory); endpoint=str(root/"control.sock")
        env=dict(os.environ,SDL_AUDIODRIVER="dummy",DAW_CONTROL_SOCKET=endpoint,DAW_PRESET_LIBRARY=str(root/"library"),DAW_COLLECTIONS_ROOT=str(root/"collections"))
        process=subprocess.Popen([fixture,endpoint,"mix-fx"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=="READY"
            async with stdio_client(StdioServerParameters(command=str(ROOT/"tools/mcp/run.sh"),env=env)) as (read,write):
                async with ClientSession(read,write) as session:
                    await session.initialize();tools={t.name:t for t in (await session.list_tools()).tools}
                    assert len(tools)==70
                    assert 'effect_offset' in tools['get_mix_capture'].inputSchema['properties']
                    for name in ("get_mix_snapshot","capture_mix","get_mix_capture"):assert tools[name].annotations.readOnlyHint
                    async def call(name,**arguments):
                        """Keep SDK transport success separate from explicit domain refusal."""
                        result=await session.call_tool(name,arguments);assert not result.isError,result
                        return result.structuredContent or json.loads(result.content[0].text)
                    capabilities=await call("capabilities");assert not capabilities['writes_enabled']
                    assert capabilities['mix_diagnostics']['full_context_seconds']==300 and capabilities['mix_diagnostics']['range_seconds']==30
                    history=fixture_command(process,"end_gesture");receipts=capabilities['receipts_used']
                    guard=await call("get_mix_snapshot");assert guard['ok'] and guard['node_count']==2
                    captured=await call("capture_mix",expected_mix=guard['expected_mix'],start_frame="0",end_frame="48000");assert captured['schema']=='sonics_mix_capture_v3'
                    assert captured['ok'] and captured['context_start_frame']=="0" and not captured['normalized'],captured
                    page=await call("get_mix_capture",capture_id=captured['capture_id']);assert page['ok'] and page['node']['kind']=='child',page
                    meter=page['node']['stages'][0]['channels'][0];assert meter['sample_count']=='48000' and meter['sample_peak']>0 and meter['rms']>0
                    loud=page['loudness'];assert loud['available'] and loud['measured_frames']=='48000' and loud['integrated_lufs'] is not None and loud['short_term_lufs'] is None
                    assert len(loud['history'])==10 and loud['history'][0]['momentary_lufs'] is None and loud['history'][3]['momentary_lufs'] is not None
                    assert len(loud['channels'])==2 and all(p['true_peak_linear']>=0 for p in loud['channels']) and 'whole_programme_loudness' in page['unavailable']
                    assert len(meter['bin_rms'])==32 and sum(meter['bin_sample_count'])==48000
                    assert page['sources'][0]['note_count']==1 and page['sources'][0]['region_relative_gate_max_frames']=='24000'
                    assert len(page['sources'][0]['supported_controls'])>0 and 'other_effect_gain_reduction' in page['unavailable']
                    assert await call("get_mix_capture",capture_id=captured['capture_id'])==page
                    master=await call("get_mix_capture",capture_id=captured['capture_id'],node_offset=1);assert master['node']['kind']=='master' and master['node']['stages'][3]['available']
                    assert fixture_command(process,"end_gesture")==history and fixture_command(process,"changes")=="DONE 0" and (await call("capabilities"))['receipts_used']==receipts and not (root/'library').exists()
                    started=time.monotonic()
                    song=await call('capture_mix',expected_mix=guard['expected_mix'],whole_song=True,tail_frames='48000');assert song['schema']=='sonics_song_loudness_v1' and song['authored_end_frame']=='120000' and song['end_frame']=='168000'
                    song_page=await call('get_mix_capture',capture_id=song['capture_id'],node_offset=1);assert song_page['loudness']['lra_provisional'] and song_page['loudness']['lra_complete_windows']==6 and song_page['loudness']['measured_frames']=='168000'
                    assert await call('get_mix_capture',capture_id=song['capture_id'],node_offset=1)==song_page
                    for bad in ({'whole_song':1},{'whole_song':True,'end_frame':'48000'},{'whole_song':True,'tail_frames':'1440001'},{'whole_song':False,'tail_frames':'1'}):
                        request=dict(op='capture_mix',expected_mix=guard['expected_mix'],start_frame='0',end_frame='1');request.update(bad);assert wire(endpoint,request)['code']=='invalid_request'
                    assert wire(endpoint,dict(op='get_mix_capture',capture_id=song['capture_id'],note_offset=1))['code']=='invalid_request'
                    fixture_command(process,"native_gain")
                    assert (await call("capture_mix",expected_mix=guard['expected_mix'],start_frame="0",end_frame="48000"))['code']=='state_conflict'
                    assert await call("get_mix_capture",capture_id=captured['capture_id'])==page
                    assert await call('get_mix_capture',capture_id=song['capture_id'],node_offset=1)==song_page
                    assert (await call('capture_mix',expected_mix=guard['expected_mix'],whole_song=True))['code']=='state_conflict'
                    fresh=await call("get_mix_snapshot");assert fresh['settings_fingerprint_fnv1a64']!=guard['settings_fingerprint_fnv1a64']
                    updated=await call("capture_mix",expected_mix=fresh['expected_mix'],start_frame="0",end_frame="48000");newpage=await call("get_mix_capture",capture_id=updated['capture_id']);assert abs(newpage['node']['stages'][0]['channels'][0]['rms']/meter['rms']-.75)<1e-5
                    fixture_command(process,"begin_gesture");assert (await call("capture_mix",expected_mix=fresh['expected_mix'],start_frame="0",end_frame="48000"))['code']=='busy';fixture_command(process,"end_gesture")
                    for extra in ({'start_frame':'00'},{'end_frame':'0'},{'end_frame':'14400001'},{'end_frame':'1440001'},{'start_frame':1},{'unknown':0}):
                        request=dict(op='capture_mix',expected_mix=fresh['expected_mix'],start_frame='0',end_frame='48000');request.update(extra);assert wire(endpoint,request)['code']=='invalid_request'
                    for extra in ({'node_offset':2},{'node_offset':.5},{'source_offset':2},{'unknown':0}):
                        request=dict(op='get_mix_capture',capture_id=updated['capture_id']);request.update(extra);assert wire(endpoint,request)['code']=='invalid_request'
                    assert page['effect_count']==2 and page['effect']['type_id']==20 and page['next_effect_offset']==1
                    gr=page['effect']['gain_reduction'];assert gr['available'] and int(gr['channels'][0]['sample_count'])==48000
                    assert gr['channels'][0]['minimum_gain_db']<0 and sum(gr['channels'][0]['bin_sample_count'])==48000
                    second=await call('get_mix_capture',capture_id=captured['capture_id'],effect_offset=1)
                    assert second['effect']['type_id']==21 and second['next_effect_offset']==-1 and second['effect']['boundaries'][1]['latency_frames']=='144'
                    assert second['effect']['gain_reduction']['channels'][0]==second['effect']['gain_reduction']['channels'][1] or second['effect']['gain_reduction']['channels'][0]['mean_gain_db']==second['effect']['gain_reduction']['channels'][1]['mean_gain_db']
                    for bad in (2,15,-1,0.5):assert wire(endpoint,dict(op='get_mix_capture',capture_id=captured['capture_id'],effect_offset=bad))['code']=='invalid_request'
                    assert await call('get_mix_capture',capture_id=captured['capture_id'],effect_offset=1)==second
                    freq=page['frequency'];assert freq['available'] and len(freq['bin_mean_square_power'])==1025 and freq['window_offset']==0
                    assert abs(sum(freq['bin_mean_square_power'])-freq['window_weighted_mean_square'])<1e-8
                    assert abs(sum(b['mean_square_power'] for b in freq['bands'])-freq['window_weighted_mean_square'])<1e-8
                    last=await call('get_mix_capture',capture_id=captured['capture_id'],frequency_window=31,frequency_channel=1)
                    assert last['frequency']['next_window_offset']==-1 and last['frequency']['next_channel_offset']==-1
                    for extra in ({'frequency_window':32},{'frequency_channel':2},{'frequency_window':.5},{'frequency_channel':-1}):
                        request=dict(op='get_mix_capture',capture_id=captured['capture_id']);request.update(extra);assert wire(endpoint,request)['code']=='invalid_request'
                    assert await call('get_mix_capture',capture_id=captured['capture_id'],frequency_window=31,frequency_channel=1)==last
                    assert page['note_count']==1 and page['next_note_offset']==-1
                    ending=page['note_endings'][0];assert ending['authored_key_off_frame']=='24000' and ending['prepared_key_off_frame']=='24000' and ending['prepared_for_capture']
                    tail=page['sources'][0]['source_tail'];assert tail['prepared'] and int(tail['channels'][0]['observed_samples'])==48000 and int(tail['channels'][0]['post_last_damper_observed_samples'])==24000
                    assert (await call('get_mix_capture',capture_id=captured['capture_id'],note_offset=1))['note_endings']==[]
                    for bad in (2,-1,.5):assert wire(endpoint,dict(op='get_mix_capture',capture_id=captured['capture_id'],note_offset=bad))['code']=='invalid_request'
                    # A longer preceding render uses full context rather than the old one-second preroll.
                    long_context=await call("capture_mix",expected_mix=fresh['expected_mix'],start_frame='528000',end_frame='576000');assert long_context['ok'] and long_context['preceding_context_frames']=='528000'
                    fixture_command(process,"replace");assert await call("get_mix_capture",capture_id=captured['capture_id'])==page
                    assert (await call("capture_mix",expected_mix=fresh['expected_mix'],start_frame='0',end_frame='48000'))['code']=='state_conflict'
                    # Endpoint retention is count-bounded and expiration never returns current project measurements.
                    fresh=await call("get_mix_snapshot")
                    for _ in range(4):assert (await call("capture_mix",expected_mix=fresh['expected_mix'],start_frame='0',end_frame='48000'))['ok']
                    assert (await call("get_mix_capture",capture_id=captured['capture_id']))['code']=='expired_mix_capture'
                    silence=await call("capture_mix",expected_mix=fresh['expected_mix'],start_frame='0',end_frame='48000');zero=await call("get_mix_capture",capture_id=silence['capture_id'],node_offset=1)
                    assert zero['node']['stages'][3]['channels'][0]['sample_peak_dbfs'] is None and zero['node']['stages'][3]['channels'][0]['rms']==0
                    assert zero['loudness']['integrated_lufs'] is None and zero['loudness']['momentary_lufs'] is None and all(p['true_peak_dbtp'] is None for p in zero['loudness']['channels'])
                    assert zero['frequency']['window_weighted_rms_dbfs'] is None and zero['frequency']['dominant_bin_hz'] is None and not any(zero['frequency']['bin_mean_square_power'])
                    assert fixture_command(process,"changes")=="DONE 0"
                    (output/'mix-sdk.json').write_text(json.dumps({'tool_count':len(tools),'read_only':True,'guard':guard,'capture':captured,'child':page,'master':master,'long_context':long_context,'immutable_across_edit_and_replacement':True,'expired_capture_refused':True,'no_authored_changes_or_receipts':True},indent=2)+'\n')
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=20);assert process.returncode==0,process.returncode
    print('mcp_mix_capture_test: passed (ordinary SDK, read-only endpoint, exact guards, channel/bucket accounting, immutable edit/replacement readback, full preceding context, silence, strict refusal and retention)')

async def project_check(fixture, project, output, whole_only=False):
    """Measure a supplied private project copy through normal SDK calls without edits or audition."""
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sonics-mix-project-") as directory:
        root=Path(directory);endpoint=str(root/'control.sock');env=dict(os.environ,SDL_AUDIODRIVER='dummy',DAW_CONTROL_SOCKET=endpoint,DAW_COLLECTIONS_ROOT=str(root/'collections'),DAW_PRESET_LIBRARY=str(root/'library'))
        process=subprocess.Popen([fixture,endpoint,'mix-project',str(project)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env=env)
        try:
            assert process.stdout.readline().strip()=='READY'
            async with stdio_client(StdioServerParameters(command=str(ROOT/'tools/mcp/run.sh'),env=env)) as (r,w):
                async with ClientSession(r,w) as session:
                    await session.initialize()
                    async def call(name,**args):
                        """Read explicit domain results through the ordinary SDK adapter."""
                        result=await session.call_tool(name,args);assert not result.isError,result
                        value=result.structuredContent or json.loads(result.content[0].text);assert value['ok'],value;return value
                    guard=await call('get_mix_snapshot')
                    if not whole_only:
                        captured=await call('capture_mix',expected_mix=guard['expected_mix'],start_frame='4800000',end_frame='5760000')
                        pages=[await call('get_mix_capture',capture_id=captured['capture_id'],node_offset=n,source_offset=min(n*8,guard['source_count'])) for n in range(guard['node_count'])]
                        assert all(p['loudness']['available'] and p['loudness']['measured_frames']=='960000' and len(p['loudness']['history'])==200 and p['loudness']['history'][-1]['end_frame']=='5760000' for p in pages)
                        assert pages[-1]['loudness']['integrated_lufs'] is not None
                        assert await call('get_mix_capture',capture_id=captured['capture_id'])==pages[0]
                        assert fixture_command(process,'changes')=='DONE 0'
                    started=time.monotonic()
                    song=await call('capture_mix',expected_mix=guard['expected_mix'],whole_song=True,tail_frames='240000');assert song['schema']=='sonics_song_loudness_v1' and int(song['authored_end_frame'])>1440000
                    elapsed=time.monotonic()-started
                    song_pages=[await call('get_mix_capture',capture_id=song['capture_id'],node_offset=n) for n in range(guard['node_count'])]
                    assert all(p['loudness']['measured_frames']==song['end_frame'] and p['loudness']['lra_complete_windows']>300 for p in song_pages)
                    assert song_pages[-1]['loudness']['integrated_lufs'] is not None and song_pages[-1]['loudness']['loudness_range_lu'] is not None and not song_pages[-1]['loudness']['lra_provisional']
                    assert fixture_command(process,'changes')=='DONE 0'
                    (output/'whole-song.json').write_text(json.dumps(dict(capture=song,node_pages=song_pages,no_edits_or_audition=True,capture_elapsed_seconds=elapsed),indent=2)+'\n')
                    assert await call('get_mix_capture',capture_id=song['capture_id'])==song_pages[0]
                    if not whole_only:
                        source_pages=[]
                        for source in range(0,guard['source_count'],8):source_pages.append((await call('get_mix_capture',capture_id=captured['capture_id'],source_offset=source))['sources'])
                        (output/'project-mix.json').write_text(json.dumps({'guard':guard,'capture':captured,'node_pages':pages,'source_pages':source_pages,'private_copy_only':True,'no_edits_or_audition':True},indent=2)+'\n')
        finally:
            if process.poll() is None:process.stdin.write('quit\n');process.stdin.flush()
            process.wait(timeout=20);assert process.returncode==0,process.returncode
    print('mcp_mix_project: passed (private project copy, actual production graph, 100–120s full-context capture, all immutable node/source pages, no edits or audition)')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--fixture',required=True);parser.add_argument('--output-root',type=Path,required=True);parser.add_argument('--project',type=Path);parser.add_argument('--whole-only',action='store_true');args=parser.parse_args()
    if args.project:asyncio.run(project_check(args.fixture,args.project,args.output_root/'mix-project',args.whole_only))
    else:asyncio.run(check(args.fixture,args.output_root/'mix-diagnostics'))
