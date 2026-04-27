/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useState, useEffect } from 'react';
import { SpectralDisplay } from './components/SpectralDisplay';
import { INITIAL_BANDS, Band } from './types';
import { audioEngine } from './services/audioEngine';
import { Settings, Zap, Play, Square, Activity, Cpu } from 'lucide-react';
import { motion, AnimatePresence } from 'motion/react';

export default function App() {
  const [bands, setBands] = useState<Band[]>(INITIAL_BANDS);
  const [selectedBandId, setSelectedBandId] = useState<string | null>(bands[0].id);
  const [isActive, setIsActive] = useState(false);
  const [analyzers, setAnalyzers] = useState<any>(null);
  const [isAiLoading, setIsAiLoading] = useState(false);
  const [grLevels, setGrLevels] = useState<Record<string, number>>({});

  useEffect(() => {
    let intervalId: any;
    if (isActive) {
      intervalId = setInterval(() => {
        const newGr: Record<string, number> = {};
        bands.forEach(b => {
          newGr[b.id] = audioEngine.getGainReduction(b.id);
        });
        setGrLevels(newGr);
      }, 50); // 20fps for meters
    }
    return () => clearInterval(intervalId);
  }, [isActive, bands]);

  useEffect(() => {
    if (isActive) {
      audioEngine.updateBands(bands);
    }
  }, [bands, isActive]);

  const toggleEngine = async () => {
    if (!isActive) {
      await audioEngine.init();
      await audioEngine.resume();
      setAnalyzers(audioEngine.getAnalyzers());
      setIsActive(true);
    } else {
      setIsActive(false);
    }
  };

  const handleAiDetect = async () => {
    if (!isActive || !analyzers?.in) return;
    
    setIsAiLoading(true);
    try {
      const spectrum = new Uint8Array(255);
      analyzers.in.getByteFrequencyData(spectrum);
      const dataArray = Array.from(spectrum);
      
      const { aiAdvisor } = await import('./services/aiAdvisor');
      const suggestedBands = await aiAdvisor.suggestBands(dataArray);
      setBands(suggestedBands);
      setSelectedBandId(suggestedBands[0]?.id || null);
    } catch (err) {
      console.error(err);
    } finally {
      setIsAiLoading(false);
    }
  };

  const selectedBand = bands.find(b => b.id === selectedBandId);

  const updateSelectedBandParams = (newParams: Partial<Band['params']>) => {
    setBands(prev => prev.map(band => 
      band.id === selectedBandId 
        ? { ...band, params: { ...band.params, ...newParams } } 
        : band
    ));
  };

  return (
    <div className="h-screen w-full flex flex-col bg-[#0f1115] font-sans text-gray-300 overflow-hidden select-none">
      {/* 1. Header Navigation */}
      <nav className="h-14 bg-[#1a1c22] border-b border-[#2a2d35] flex items-center justify-between px-6 shrink-0">
        <div className="flex items-center gap-4">
          <div className="w-8 h-8 bg-[#3b82f6] rounded flex items-center justify-center font-bold text-white shadow-lg shadow-blue-500/20">MB</div>
          <div className="flex flex-col">
            <span className="text-sm font-bold tracking-tight text-white uppercase italic">NovaMB Pro <span className="text-[#6b7280] text-[10px] font-normal not-italic ml-2 tracking-widest">v2.1.0</span></span>
          </div>
        </div>
        
        <div className="flex items-center gap-6">
          <div className="flex items-center bg-[#0a0b0d] rounded-md px-4 py-1.5 border border-[#333] cursor-pointer hover:border-[#444] transition-colors">
            <span className="text-[#6b7280] text-[10px] uppercase font-bold mr-3 tracking-tighter">Preset:</span>
            <span className="text-white text-xs font-semibold">Mastering - Pure Punch</span>
          </div>
          
          <div className="flex items-center gap-4">
            <button 
              onClick={handleAiDetect}
              disabled={isAiLoading || !isActive}
              className={`flex items-center gap-2 px-4 py-1.5 rounded bg-indigo-500/10 border border-indigo-500/20 text-indigo-400 text-[10px] font-bold uppercase transition-all ${isAiLoading ? 'animate-pulse opacity-50' : 'hover:bg-indigo-500/20'}`}
            >
              <Cpu size={12} className={isAiLoading ? 'animate-spin' : ''} />
              {isAiLoading ? 'Analyzing...' : 'AI ASSIST'}
            </button>
            
            <button 
              onClick={toggleEngine}
              className={`flex items-center gap-2 px-6 py-1.5 rounded font-black text-[10px] uppercase tracking-widest transition-all ${
                isActive 
                  ? 'bg-red-500 text-white shadow-lg shadow-red-500/20' 
                  : 'bg-blue-600 text-white shadow-lg shadow-blue-600/20 hover:bg-blue-500'
              }`}
            >
              {isActive ? <Square size={10} fill="currentColor" /> : <Play size={10} fill="currentColor" />}
              {isActive ? 'OFF' : 'ON'}
            </button>
          </div>
        </div>
      </nav>

      {/* 2. Analyzer Visualizer */}
      <div className="flex-1 p-4 relative min-h-0">
        <div className="w-full h-full bg-[#050607] rounded-xl border border-[#2a2d35] relative overflow-hidden ring-1 ring-white/5">
          <SpectralDisplay 
            bands={bands}
            onBandChange={setBands}
            inputAnalyzer={analyzers?.in}
            sidechainAnalyzer={analyzers?.sidechain}
            outputAnalyzer={analyzers?.out}
          />
        </div>
      </div>

      {/* 3. Settings Panel */}
      <div className="h-72 bg-[#14161b] border-t border-[#2a2d35] flex shrink-0">
        {/* Band Selector */}
        <div className="w-20 border-r border-[#2a2d35] flex flex-col items-center justify-center gap-4 overflow-y-auto py-4 scrollbar-none">
          {bands.map((band, idx) => (
            <button
              key={band.id}
              onClick={() => setSelectedBandId(band.id)}
              className={`w-10 h-10 rounded border transition-all flex items-center justify-center text-xs font-bold ${
                selectedBandId === band.id 
                  ? 'border-2 border-accent-yellow bg-[#1e1f24] text-accent-yellow ring-2 ring-accent-yellow/20' 
                  : 'border-[#444] text-[#6b7280] opacity-50 hover:opacity-100'
              }`}
              style={selectedBandId === band.id ? {} : { color: band.color }}
            >
              {idx + 1}
            </button>
          ))}
          <button className="w-8 h-8 rounded-full bg-[#333] flex items-center justify-center text-gray-400 font-light hover:bg-[#444] transition-colors">+</button>
        </div>

        {/* Parameters Grid */}
        <div className="flex-1 p-8 overflow-hidden">
          <AnimatePresence mode="wait">
            {selectedBand && (
              <motion.div 
                key={selectedBand.id}
                initial={{ opacity: 0, y: 10 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -10 }}
                className="h-full flex gap-12"
              >
                  <div className="flex-1 grid grid-cols-5 gap-10">
                    <CircularKnob 
                      label="Threshold" 
                      value={selectedBand.params.threshold} 
                      displayValue={selectedBand.params.threshold.toFixed(1) + 'dB'}
                      min={-60} max={0} color={selectedBand.color}
                      gr={grLevels[selectedBand.id] || 0}
                      onChange={(v) => updateSelectedBandParams({ threshold: v })}
                    />
                  <CircularKnob 
                    label="Range" 
                    value={selectedBand.params.range} 
                    displayValue={selectedBand.params.range.toFixed(1) + 'dB'}
                    min={0} max={30} color={selectedBand.color}
                    onChange={(v) => updateSelectedBandParams({ range: v })}
                  />
                  <CircularKnob 
                    label="Ratio" 
                    value={selectedBand.params.ratio} 
                    displayValue={selectedBand.params.ratio.toFixed(1) + ':1'}
                    min={1} max={20} color="#3b82f6"
                    onChange={(v) => updateSelectedBandParams({ ratio: v })}
                  />
                  <CircularKnob 
                    label="Attack" 
                    value={selectedBand.params.attack} 
                    displayValue={selectedBand.params.attack.toFixed(0) + 'ms'}
                    min={0.1} max={500} color="#3b82f6"
                    onChange={(v) => updateSelectedBandParams({ attack: v })}
                  />
                  <CircularKnob 
                    label="Release" 
                    value={selectedBand.params.release} 
                    displayValue={selectedBand.params.release.toFixed(0) + 'ms'}
                    min={10} max={2000} color="#3b82f6"
                    onChange={(v) => updateSelectedBandParams({ release: v })}
                  />
                </div>

                <div className="w-64 border-l border-[#2a2d35] pl-10 flex flex-col justify-center gap-6">
                  <div className="space-y-2">
                    <span className="text-[10px] font-black text-[#6b7280] uppercase tracking-widest">Sidechain</span>
                    <button 
                      onClick={() => updateSelectedBandParams({ sidechainExternal: !selectedBand.params.sidechainExternal })}
                      className={`w-full py-2.5 rounded border text-[10px] font-black uppercase tracking-tighter transition-all ${
                        selectedBand.params.sidechainExternal 
                          ? 'bg-blue-600 border-blue-400 text-white ring-4 ring-blue-600/10' 
                          : 'bg-[#0a0b0d] border-[#333] text-[#6b7280] hover:border-[#444]'
                      }`}
                    >
                      {selectedBand.params.sidechainExternal ? 'External SC: ON' : 'External SC: OFF'}
                    </button>
                  </div>
                  
                  <div className="space-y-3">
                    <div className="flex justify-between items-center text-[10px] font-black text-[#6b7280] uppercase tracking-widest">
                       <span>Knee</span>
                       <span className="text-white">{selectedBand.params.knee > 15 ? 'SOFT' : 'HARD'}</span>
                    </div>
                    <div className="w-full h-1 bg-[#1a1c22] rounded overflow-hidden">
                      <div className="h-full bg-blue-500" style={{ width: `${(selectedBand.params.knee / 30) * 100}%` }}></div>
                    </div>
                  </div>
                </div>
              </motion.div>
            )}
          </AnimatePresence>
        </div>
      </div>

      {/* 4. Bottom Toolbar */}
      <footer className="h-12 bg-[#1a1c22] border-t border-[#2a2d35] flex items-center px-6 justify-between shrink-0">
        <div className="flex gap-8 items-center h-full">
           <div className="flex items-center gap-2">
             <span className="text-[10px] font-bold text-[#6b7280] uppercase tracking-widest">Phase:</span>
             <span className="text-xs text-white uppercase font-mono">Dynamic</span>
           </div>
           <div className="flex items-center gap-2 border-l border-[#333] pl-8">
             <span className="text-[10px] font-bold text-[#6b7280] uppercase tracking-widest">Over:</span>
             <span className="text-xs text-white uppercase font-mono">4X</span>
           </div>
        </div>

        <div className="flex items-center gap-10">
          <div className="flex items-center gap-4">
            <span className="text-[10px] font-bold text-[#6b7280] uppercase tracking-widest">Dry/Wet</span>
            <div className="w-40 h-1.5 bg-[#0a0b0d] rounded-full relative overflow-hidden border border-[#333]">
              <div className="h-full bg-[#3b82f6] w-full"></div>
            </div>
            <span className="text-xs font-mono text-white w-8">100%</span>
          </div>
          
          <div className="flex items-center gap-4 border-l border-[#333] pl-8">
            <span className="text-[10px] font-bold text-[#6b7280] uppercase tracking-widest text-accent-yellow">Output</span>
            <div className="flex gap-0.5 h-4 w-12 items-end pb-0.5">
              {[...Array(6)].map((_, i) => (
                <div key={i} className={`w-1 h-full rounded-sm ${i < 4 ? 'bg-green-500' : 'bg-gray-800'}`} />
              ))}
            </div>
            <span className="text-xs font-mono text-white">-0.2dB</span>
          </div>
        </div>
      </footer>
    </div>
  );
}

function CircularKnob({ 
  label, 
  value, 
  displayValue, 
  min, 
  max, 
  color, 
  gr = 0,
  onChange 
}: { 
  label: string; 
  value: number; 
  displayValue: string;
  min: number; 
  max: number;
  color: string;
  gr?: number;
  onChange: (val: number) => void;
}) {
  const [isDragging, setIsDragging] = useState(false);

  useEffect(() => {
    const handleGlobalMouseMove = (e: MouseEvent) => {
      if (!isDragging) return;
      const step = (max - min) / 300;
      const delta = -e.movementY * step;
      onChange(Math.min(max, Math.max(min, value + delta)));
    };
    const handleUp = () => setIsDragging(false);

    if (isDragging) {
      window.addEventListener('mousemove', handleGlobalMouseMove);
      window.addEventListener('mouseup', handleUp);
    }
    return () => {
      window.removeEventListener('mousemove', handleGlobalMouseMove);
      window.removeEventListener('mouseup', handleUp);
    };
  }, [isDragging, value, min, max, onChange]);

  const rotation = -150 + ((value - min) / (max - min)) * 300;

  return (
    <div className="flex flex-col items-center gap-4 group cursor-ns-resize" onMouseDown={() => setIsDragging(true)}>
      <div className="relative w-24 h-24 flex items-center justify-center">
        {/* Track */}
        <svg className="absolute inset-0 w-full h-full -rotate-[240deg]" viewBox="0 0 100 100">
          <circle cx="50" cy="50" r="42" fill="none" stroke="#222" strokeWidth="6" strokeDasharray="210 314" strokeLinecap="round" />
          <circle 
            cx="50" cy="50" r="42" fill="none" stroke={color} strokeWidth="6" 
            strokeDasharray={`${((value - min) / (max - min)) * 210} 314`} 
            strokeLinecap="round" 
            className="transition-all duration-300"
          />
        </svg>
        
        {/* Gain Reduction Meter Overlay (Semi-circle) */}
        {gr < -0.1 && (
           <svg className="absolute inset-0 w-full h-full rotate-[120deg]" viewBox="0 0 100 100">
              <circle 
                cx="50" cy="50" r="48" fill="none" stroke="#ef4444" strokeWidth="2" 
                strokeDasharray={`${Math.abs(gr) * 4} 314`} 
                strokeLinecap="round" 
              />
           </svg>
        )}
        
        {/* Knob Body */}
        <div className="w-16 h-16 rounded-full bg-[#1e1f24] border-2 border-[#121417] shadow-xl flex items-center justify-center relative overflow-hidden group-hover:bg-[#25272d] transition-colors">
           <div className="absolute top-2 w-1 h-3 bg-white/20 rounded-full" style={{ transform: `rotate(${rotation}deg)`, transformOrigin: '50% 170%' }} />
           <span className="text-xs font-black text-white tracking-widest">{displayValue}</span>
        </div>
      </div>
      <span className="text-[9px] font-black uppercase tracking-[0.2em] text-[#6b7280] group-hover:text-white transition-colors">{label}</span>
    </div>
  );
}

