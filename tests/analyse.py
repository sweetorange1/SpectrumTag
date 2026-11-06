import json,sys
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy import signal,ndimage
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(sys.argv[1])
changed=sys.argv[2] if len(sys.argv)>2 else 'final'
target=np.array(Image.open(root/'resolution-chart.png').convert('L'))<128
analyser=2048; hop=128
results=[]; views=[]
for n in (1024,2048,4096,8192):
    pair=[]
    for build in ('baseline',changed):
        directory=root/f'{build}-{n}'
        sr,a=wavfile.read(directory/'input.wav'); _,b=wavfile.read(directory/'output.wav')
        a=a.astype(float)/2147483648; b=b.astype(float)/2147483648
        latency=n-1
        b=b[latency:];a=a[:len(b)]
        fs,ts,A=signal.stft(a,fs=sr,nperseg=analyser,noverlap=analyser-hop,axis=0)
        _,_,B=signal.stft(b,fs=sr,nperseg=analyser,noverlap=analyser-hop,axis=0)
        # Stereo power and identical input reference reduce random noise variance.
        pa=np.mean(np.abs(A)**2,axis=1);pb=np.mean(np.abs(B)**2,axis=1)
        pa=ndimage.gaussian_filter(pa,(0.6,0.7));pb=ndimage.gaussian_filter(pb,(0.6,0.7))
        db=10*np.log10((pb+1e-15)/(pa+1e-15))
        fy=np.linspace(0.9*sr/2,0.1*sr/2,target.shape[0])/ (sr/analyser)
        best=None
        # Register only a global time translation; duration and frequency extent are fixed.
        for start in np.arange(2.0,2.8,hop/sr/2):
            tx=(start+np.linspace(0,8,target.shape[1]))/(hop/sr)
            view=ndimage.map_coordinates(db,np.array(np.meshgrid(fy,tx,indexing='ij')),order=1)
            corr=np.corrcoef(target.ravel(),np.clip(-view.ravel(),0,30))[0,1]
            if best is None or corr>best[0]:best=(corr,start,view)
        corr,start,view=best
        ink=ndimage.binary_erosion(target,iterations=1)
        paper=ndimage.binary_erosion(~target,iterations=2)
        contrast=float(np.median(view[paper])-np.median(view[ink]))
        record={'fft':n,'build':build,'mask_correlation':float(corr),'stroke_contrast_db':contrast,'registered_start_sec':float(start)}
        results.append(record);pair.append(view)
    views.append(pair)
(root/'metrics.json').write_text(json.dumps(results,indent=2))
fig,axes=plt.subplots(4,3,figsize=(14,13),constrained_layout=True)
for row,n in enumerate((1024,2048,4096,8192)):
    axes[row,0].imshow(target,cmap='gray_r',aspect='auto');axes[row,0].set_title(f'Target | DSP FFT {n}')
    for col,label in enumerate(('Original 1.4.3','Realtime 1.4.4'),1):
        im=axes[row,col].imshow(views[row][col-1],vmin=-30,vmax=0,cmap='inferno',aspect='auto')
        r=results[row*2+col-1]
        axes[row,col].set_title(f"{label} | contrast {r['stroke_contrast_db']:.1f} dB | r={r['mask_correlation']:.3f}")
    for ax in axes[row]:ax.set_xticks([]);ax.set_yticks([])
fig.suptitle('Actual processed white noise | same input, 48 kHz, stereo, 8-second stamp\nCommon 2048-point analyser; attenuation relative to input. Brighter = retained sound.',fontsize=12)
fig.colorbar(im,ax=axes[:,2],label='Attenuation (dB)',shrink=.6)
fig.savefig(root/'audio-comparison.png',dpi=140)
print(json.dumps(results,indent=2))
