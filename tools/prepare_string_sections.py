"""Prepare only viola, violin-section and bowed double-bass sustain banks with existing loop/energy tools."""
import argparse,pathlib,subprocess,sys
p=argparse.ArgumentParser(description=__doc__);p.add_argument('bank_root');args=p.parse_args()
for script in ['prepare_flute_cello_samples.py','stabilize_flute_cello_samples.py','refine_flute_cello_samples.py']:
    if script=='stabilize_flute_cello_samples.py':
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('align_oboe_horn_samples.py')),args.bank_root,'--banks','vsco2_violin_section_v1'],check=True)
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('prepare_flute_cello_samples.py')),args.bank_root,'--banks','vsco2_violin_section_v1','--prepared'],check=True)
    subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name(script)),args.bank_root,'--banks','vsco2_viola_section_v1','vsco2_violin_section_v1','vsco2_double_bass_v1'],check=True)
