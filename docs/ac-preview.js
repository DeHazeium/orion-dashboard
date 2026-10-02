// Display-only suggestion. No command is sent to an air conditioner.
export function previewTemperature(people) {
  if (!Number.isInteger(people) || people < 0) return null;
  if (people === 0) return 'OFF';
  if (people === 1) return 24;
  if (people === 2) return 22;
  if (people === 3) return 20;
  return 16;
}
