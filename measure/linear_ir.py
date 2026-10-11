import json, numpy as np, soundfile as sf
man=json.load(open('measure/saturator_test_signal.json')); sr=48000
seg=[s for s in man['segments'] if s['name'].startswith('sweep')][0]
def lin_H(path, nfft=1<<16):
    y,_=sf.read(path); y=y[:,0]
    a=int(round(seg['start']*sr)); n=int(round(seg['length']*sr))
    ys=y[a:a+n+sr]
    T=seg['length']; t=np.arange(n)/sr; L=T/np.log(seg['f2']/seg['f1'])
    x=np.sin(2*np.pi*seg['f1']*L*(np.exp(t/L)-1)) * 10**(-18/20)
    # fades as generated
    m=int(sr*0.005); r=0.5-0.5*np.cos(np.linspace(0,np.pi,m)); x[:m]*=r; x[-m:]*=r[::-1]
    N=1<<int(np.ceil(np.log2(len(ys)+n)))
    X=np.fft.rfft(x,N); Y=np.fft.rfft(ys,N)
    f=np.fft.rfftfreq(N,1/sr)
    # regularized deconvolution inside sweep band
    H=Y*np.conj(X)/(np.abs(X)**2+1e-3*np.max(np.abs(X))**2*((f<f.max()*0+15)|(f>23000)))
    h=np.fft.irfft(H,N)
    pk=np.argmax(np.abs(h[:4000]))
    hl=h[:8192].copy(); win=np.ones(8192); win[4096:]=np.hanning(8192)[4096:]; hl*=win
    Hf=np.fft.rfft(hl,nfft); ff=np.fft.rfftfreq(nfft,1/sr)
    return ff,Hf,hl,pk
if __name__=='__main__':
    for d in [0,25]:
        ff,H,h,pk=lin_H(f'prints/bbtubes_drive{d}.wav')
        print('drive',d,'IR peak idx',pk)
        for p in [10,15,20,25,30,40,50,60,70,80,100,150,200,300,500,1000,2000,3000,5000,8000,10000,14000,16000,18000,19000,20000,21000,22000]:
            i=np.argmin(np.abs(ff-p)); print(f'{p:6d} {20*np.log10(abs(H[i])):7.2f} dB  {np.degrees(np.angle(H[i]*np.exp(2j*np.pi*ff[i]*1/sr))):7.1f} deg')
        np.save(f'measure/ir_d{d}.npy',h)
