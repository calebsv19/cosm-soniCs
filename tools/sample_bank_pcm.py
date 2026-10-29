"""Decode original PCM recordings for offline fixed-bank preparation."""
import pathlib, wave, numpy as np

def decode_centered_pcm(path):
    """Read mono/stereo 16-bit or 24-bit PCM at its native rate without modifying the original WAV."""
    with wave.open(str(pathlib.Path(path))) as wav:
        rate=wav.getframerate();channels=wav.getnchannels();width=wav.getsampwidth()
        if wav.getcomptype()!='NONE' or channels not in (1,2) or width not in (2,3):
            raise ValueError('Fixed sample banks require mono/stereo 16-bit or 24-bit PCM')
        raw=wav.readframes(wav.getnframes())
    if width==3:
        b=np.frombuffer(raw,np.uint8).reshape(-1,3).astype(np.int32)
        value=b[:,0]|b[:,1]<<8|b[:,2]<<16
        value=np.where(value&0x800000,value-0x1000000,value)/8388608.
    else:value=np.frombuffer(raw,'<i2').astype(float)/32768.
    return rate,value.reshape(-1,channels).mean(axis=1)
