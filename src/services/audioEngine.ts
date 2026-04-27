import { Band, BandType } from '../types';

/**
 * NovaMB Audio Engine Core
 * Handles the Web Audio API context, filtering, and compression logic.
 */
export class AudioEngine {
  private ctx: AudioContext | null = null;
  private inputNode: GainNode | null = null;
  private outputNode: GainNode | null = null;
  private sidechainNode: GainNode | null = null;
  
  private analyzerIn: AnalyserNode | null = null;
  private analyzerOut: AnalyserNode | null = null;
  private analyzerSidechain: AnalyserNode | null = null;

  private activeBands: Map<string, {
    loPass: BiquadFilterNode;
    loPass2: BiquadFilterNode; // For 4th order Linkwitz-Riley
    hiPass: BiquadFilterNode;
    hiPass2: BiquadFilterNode;
    compressor: DynamicsCompressorNode;
    gain: GainNode;
  }> = new Map();

  async init() {
    if (this.ctx) return;
    this.ctx = new (window.AudioContext || (window as any).webkitAudioContext)();
    
    this.inputNode = this.ctx.createGain();
    this.outputNode = this.ctx.createGain();
    this.sidechainNode = this.ctx.createGain();

    this.analyzerIn = this.ctx.createAnalyser();
    this.analyzerIn.fftSize = 2048;
    this.analyzerIn.smoothingTimeConstant = 0.8;
    
    this.analyzerOut = this.ctx.createAnalyser();
    this.analyzerOut.fftSize = 2048;

    this.analyzerSidechain = this.ctx.createAnalyser();
    this.analyzerSidechain.fftSize = 2048;

    this.inputNode.connect(this.analyzerIn);
    this.sidechainNode.connect(this.analyzerSidechain);
    
    this.outputNode.connect(this.analyzerOut);
    this.analyzerOut.connect(this.ctx.destination);
  }

  updateBands(bands: Band[]) {
    if (!this.ctx || !this.inputNode || !this.outputNode) return;

    this.activeBands.forEach(nodes => {
      nodes.gain.disconnect();
      nodes.compressor.disconnect();
      nodes.hiPass.disconnect();
      nodes.hiPass2.disconnect();
      nodes.loPass.disconnect();
      nodes.loPass2.disconnect();
    });
    this.activeBands.clear();

    bands.forEach(band => {
      if (!band.enabled) return;

      // Linkwitz-Riley 4th order = Two 2nd order Butterworth in series
      const loPass = this.ctx!.createBiquadFilter();
      loPass.type = 'lowpass';
      loPass.frequency.value = band.frequencyHigh;
      loPass.Q.value = 0.707;

      const loPass2 = this.ctx!.createBiquadFilter();
      loPass2.type = 'lowpass';
      loPass2.frequency.value = band.frequencyHigh;
      loPass2.Q.value = 0.707;

      const hiPass = this.ctx!.createBiquadFilter();
      hiPass.type = 'highpass';
      hiPass.frequency.value = band.frequencyLow;
      hiPass.Q.value = 0.707;

      const hiPass2 = this.ctx!.createBiquadFilter();
      hiPass2.type = 'highpass';
      hiPass2.frequency.value = band.frequencyLow;
      hiPass2.Q.value = 0.707;

      const compressor = this.ctx!.createDynamicsCompressor();
      compressor.threshold.setTargetAtTime(band.params.threshold, this.ctx!.currentTime, 0.01);
      compressor.ratio.setTargetAtTime(band.params.ratio, this.ctx!.currentTime, 0.01);
      compressor.attack.setTargetAtTime(band.params.attack / 1000, this.ctx!.currentTime, 0.01);
      compressor.release.setTargetAtTime(band.params.release / 1000, this.ctx!.currentTime, 0.01);
      compressor.knee.setTargetAtTime(band.params.knee, this.ctx!.currentTime, 0.01);

      const gain = this.ctx!.createGain();
      gain.gain.setTargetAtTime(Math.pow(10, band.params.makeUpGain / 20), this.ctx!.currentTime, 0.01);

      // Chain: Input -> LoPass -> LoPass2 -> HiPass -> HiPass2 -> Comp -> Gain -> Output
      this.inputNode!.connect(loPass);
      loPass.connect(loPass2);
      loPass2.connect(hiPass);
      hiPass.connect(hiPass2);
      hiPass2.connect(compressor);
      compressor.connect(gain);
      gain.connect(this.outputNode!);

      this.activeBands.set(band.id, { loPass, loPass2, hiPass, hiPass2, compressor, gain });
    });
  }

  getGainReduction(bandId: string): number {
    const nodes = this.activeBands.get(bandId);
    if (!nodes) return 0;
    // reduction property is in dB. Negative values.
    return nodes.compressor.reduction;
  }

  getAudioContext() {
    return this.ctx;
  }

  getAnalyzers() {
    return {
      in: this.analyzerIn,
      out: this.analyzerOut,
      sidechain: this.analyzerSidechain
    };
  }

  async resume() {
    if (this.ctx?.state === 'suspended') {
      await this.ctx.resume();
    }
  }

  setMasterGain(value: number) {
    if (this.outputNode && this.ctx) {
      this.outputNode.gain.setTargetAtTime(value, this.ctx.currentTime, 0.05);
    }
  }
}

export const audioEngine = new AudioEngine();
