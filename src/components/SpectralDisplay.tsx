import React, { useEffect, useRef, useState } from 'react';
import * as d3 from 'd3';
import { motion } from 'motion/react';
import { Band } from '../types';
import { audioEngine } from '../services/audioEngine';

interface SpectralDisplayProps {
  bands: Band[];
  onBandChange: (bands: Band[]) => void;
  inputAnalyzer: AnalyserNode | null;
  sidechainAnalyzer: AnalyserNode | null;
  outputAnalyzer: AnalyserNode | null;
}

export const SpectralDisplay: React.FC<SpectralDisplayProps> = ({
  bands,
  onBandChange,
  inputAnalyzer,
  sidechainAnalyzer,
  outputAnalyzer,
}) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [dimensions, setDimensions] = useState({ width: 0, height: 0 });

  useEffect(() => {
    const updateSize = () => {
      if (containerRef.current) {
        setDimensions({
          width: containerRef.current.clientWidth,
          height: containerRef.current.clientHeight,
        });
      }
    };
    window.addEventListener('resize', updateSize);
    updateSize();
    return () => window.removeEventListener('resize', updateSize);
  }, []);

  useEffect(() => {
    if (!canvasRef.current || dimensions.width === 0) return;

    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const inputData = new Uint8Array(inputAnalyzer?.frequencyBinCount || 1024);
    const sidechainData = new Uint8Array(sidechainAnalyzer?.frequencyBinCount || 1024);
    const outputData = new Uint8Array(outputAnalyzer?.frequencyBinCount || 1024);

    const xScale = d3.scaleLog()
      .domain([20, 20000])
      .range([0, dimensions.width]);

    const yScale = d3.scaleLinear()
      .domain([0, 255])
      .range([dimensions.height, 0]);

    let animationId: number;

    const render = () => {
      ctx.clearRect(0, 0, dimensions.width, dimensions.height);

      // Draw Grid
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
      ctx.lineWidth = 1;
      [100, 500, 1000, 5000, 10000].forEach(freq => {
        const x = xScale(freq);
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, dimensions.height);
        ctx.stroke();
      });

      // Fetch Analysis
      inputAnalyzer?.getByteFrequencyData(inputData);
      sidechainAnalyzer?.getByteFrequencyData(sidechainData);
      outputAnalyzer?.getByteFrequencyData(outputData);

      // Draw Sidechain Spectrum (Ghosted)
      if (sidechainAnalyzer) {
        ctx.fillStyle = 'rgba(239, 68, 68, 0.15)'; // red ghost
        ctx.beginPath();
        ctx.moveTo(0, dimensions.height);
        for (let i = 0; i < sidechainData.length; i++) {
          const freq = (i * sidechainAnalyzer.context.sampleRate) / (sidechainAnalyzer.fftSize);
          if (freq < 20) continue;
          const x = xScale(freq);
          const y = yScale(sidechainData[i] * 0.8); // slight offset for ghost effect
          ctx.lineTo(x, y);
        }
        ctx.lineTo(dimensions.width, dimensions.height);
        ctx.fill();
      }

      // Draw Input Spectrum
      ctx.fillStyle = 'rgba(59, 130, 246, 0.3)'; // blue fill
      ctx.beginPath();
      ctx.moveTo(0, dimensions.height);
      for (let i = 0; i < inputData.length; i++) {
        if (!inputAnalyzer) break;
        const freq = (i * inputAnalyzer.context.sampleRate) / (inputAnalyzer.fftSize);
        if (freq < 20) continue;
        const x = xScale(freq);
        const y = yScale(inputData[i]);
        ctx.lineTo(x, y);
      }
      ctx.lineTo(dimensions.width, dimensions.height);
      ctx.fill();

      // Draw Bands Overlays
      bands.forEach(band => {
        if (!band.enabled) return;
        const xLow = xScale(band.frequencyLow);
        const xHigh = xScale(band.frequencyHigh);
        ctx.fillStyle = band.color + '22';
        ctx.fillRect(xLow, 0, xHigh - xLow, dimensions.height);

        // Draw Crossover Handles
        ctx.strokeStyle = band.color;
        ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(xLow, 0); ctx.lineTo(xLow, dimensions.height); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(xHigh, 0); ctx.lineTo(xHigh, dimensions.height); ctx.stroke();

        // Draw Gain Reduction Indicator (FabFilter style)
        const gr = audioEngine.getGainReduction ? audioEngine.getGainReduction(band.id) : 0;
        if (gr < -0.1) {
           const grHeight = Math.abs(gr) * 10; // Scaling for visibility
           ctx.fillStyle = "#ef444455"; // red block for GR
           ctx.fillRect(xLow + 2, 2, xHigh - xLow - 4, grHeight);
           
           ctx.font = '8px monospace';
           ctx.fillStyle = '#ef4444';
           ctx.fillText(`${gr.toFixed(1)}dB`, xLow + 5, grHeight + 12);
        }
      });

      animationId = requestAnimationFrame(render);
    };

    render();
    return () => cancelAnimationFrame(animationId);
  }, [dimensions, bands, inputAnalyzer, sidechainAnalyzer, outputAnalyzer]);

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!containerRef.current || e.buttons !== 1) return;
    
    const rect = containerRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const xScale = d3.scaleLog().domain([20, 20000]).range([0, dimensions.width]);
    const freq = xScale.invert(x);

    // Find closest crossover and update it
    const updatedBands = [...bands];
    let closestBand = -1;
    let type: 'low' | 'high' = 'low';
    let minDist = Infinity;

    bands.forEach((band, i) => {
      const dLow = Math.abs(xScale(band.frequencyLow) - x);
      const dHigh = Math.abs(xScale(band.frequencyHigh) - x);
      if (dLow < minDist) { minDist = dLow; closestBand = i; type = 'low'; }
      if (dHigh < minDist) { minDist = dHigh; closestBand = i; type = 'high'; }
    });

    if (closestBand !== -1 && minDist < 20) {
      if (type === 'low') updatedBands[closestBand].frequencyLow = Math.max(20, Math.min(freq, updatedBands[closestBand].frequencyHigh - 10));
      else updatedBands[closestBand].frequencyHigh = Math.min(20000, Math.max(freq, updatedBands[closestBand].frequencyLow + 10));
      onBandChange(updatedBands);
    }
  };

  return (
    <div 
      ref={containerRef} 
      onMouseMove={handleMouseMove}
      className="w-full h-full relative bg-[#121417] rounded-lg overflow-hidden border border-white/5 cursor-crosshair"
    >
      <canvas
        ref={canvasRef}
        width={dimensions.width}
        height={dimensions.height}
        className="block"
      />
      {/* Interactive Overlay Layer would go here for drag logic */}
      <div className="absolute top-2 right-2 flex gap-2">
        <span className="text-[10px] text-blue-400 font-mono">INPUT</span>
        <span className="text-[10px] text-red-400 font-mono">SIDECHAIN</span>
      </div>
    </div>
  );
};
