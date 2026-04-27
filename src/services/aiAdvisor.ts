import { GoogleGenAI, Type } from "@google/genai";
import { Band, INITIAL_BANDS } from "../types";

/**
 * NovaMB AI Smart Advisor
 * Uses Gemini 3 Flash to analyze spectral fingerprints and suggest compression bands.
 */
export class AIAdvisor {
  private ai: GoogleGenAI;

  constructor() {
    this.ai = new GoogleGenAI({ apiKey: process.env.GEMINI_API_KEY || '' });
  }

  async suggestBands(spectralData: number[]): Promise<Band[]> {
    const prompt = `
      You are a world-class mastering engineer. 
      Analyze this spectral frequency data (sampled from 20Hz to 20kHz, 255 bins). 
      The values represent magnitude.
      
      DATA: ${spectralData.join(',')}

      Based on this spectrum, suggest the optimal 3-band crossover points (Low, Mid, High) and 
      basic compression parameters (Threshold, Ratio) to balance the dynamics.

      FORMAT: Return JSON only.
    `;

    try {
      const response = await this.ai.models.generateContent({
        model: "gemini-3-flash-preview",
        contents: prompt,
        config: {
          responseMimeType: "application/json",
          responseSchema: {
            type: Type.ARRAY,
            items: {
              type: Type.OBJECT,
              properties: {
                name: { type: Type.STRING },
                frequencyLow: { type: Type.NUMBER },
                frequencyHigh: { type: Type.NUMBER },
                threshold: { type: Type.NUMBER },
                ratio: { type: Type.NUMBER },
                color: { type: Type.STRING },
              },
              required: ["name", "frequencyLow", "frequencyHigh", "threshold", "ratio", "color"]
            }
          }
        }
      });

      const suggestions = JSON.parse(response.text || '[]');
      
      return suggestions.map((s: any, index: number) => ({
        id: `ai-band-${index}`,
        name: s.name,
        enabled: true,
        frequencyLow: s.frequencyLow,
        frequencyHigh: s.frequencyHigh,
        color: s.color,
        params: {
          threshold: s.threshold,
          ratio: s.ratio,
          attack: 30,
          release: 150,
          knee: 6,
          range: 12,
          lookahead: 0,
          makeUpGain: 0,
          sidechainExternal: false,
        }
      }));
    } catch (error) {
      console.error("AI Advice failed:", error);
      return INITIAL_BANDS;
    }
  }
}

export const aiAdvisor = new AIAdvisor();
