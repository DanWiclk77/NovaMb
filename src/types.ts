/**
 * NovaMB Constants & Types
 */

export enum BandType {
  LOW = 'low',
  MID = 'mid',
  HIGH = 'high',
  CUSTOM = 'custom'
}

export interface CompressorParams {
  threshold: number; // dB
  ratio: number;
  attack: number; // ms
  release: number; // ms
  knee: number; // dB
  range: number; // dB (FabFilter style range control)
  lookahead: number; // ms
  makeUpGain: number; // dB
  sidechainExternal: boolean;
}

export interface Band {
  id: string;
  name: string;
  enabled: boolean;
  frequencyLow: number;
  frequencyHigh: number;
  params: CompressorParams;
  color: string;
}

export const DEFAULT_BAND_PARAMS: CompressorParams = {
  threshold: -20,
  ratio: 4,
  attack: 20,
  release: 100,
  knee: 6,
  range: 12,
  lookahead: 0,
  makeUpGain: 0,
  sidechainExternal: false,
};

export const INITIAL_BANDS: Band[] = [
  {
    id: 'band-1',
    name: 'Low',
    enabled: true,
    frequencyLow: 20,
    frequencyHigh: 200,
    params: { ...DEFAULT_BAND_PARAMS },
    color: '#3b82f6', // blue
  },
  {
    id: 'band-2',
    name: 'Mid',
    enabled: true,
    frequencyLow: 200,
    frequencyHigh: 2000,
    params: { ...DEFAULT_BAND_PARAMS },
    color: '#10b981', // emerald
  },
  {
    id: 'band-3',
    name: 'High',
    enabled: true,
    frequencyLow: 2000,
    frequencyHigh: 20000,
    params: { ...DEFAULT_BAND_PARAMS },
    color: '#f59e0b', // amber
  }
];
