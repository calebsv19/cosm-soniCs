"""Prepare only bassoon/tenor trombone originals, aligned sustain takes, common loops and energy curves."""
import argparse, pathlib, subprocess, sys
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('bank_root',type=pathlib.Path);args=parser.parse_args()
steps=[('prepare_flute_cello_samples.py',False),('align_oboe_horn_samples.py',None),('prepare_flute_cello_samples.py',True),('stabilize_flute_cello_samples.py',False),('refine_flute_cello_samples.py',False)]
for script,prepared in steps:
    command=[sys.executable,str(pathlib.Path(__file__).with_name(script)),str(args.bank_root)]
    command+=['--banks','vsco2_bassoon_v1','vsco2_tenor_trombone_v1']
    if script=='refine_flute_cello_samples.py':command+=['--refine-new-winds']
    if prepared:command+=['--prepared']
    subprocess.run(command,check=True)
