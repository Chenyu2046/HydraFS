const AI_MD_SECTION = [
  '# Knowledge Base Document',
  '',
  '## Summary',
  'This synthetic document represents a technical knowledge-base import with prose, structured data, and source links.',
  '',
  '### Implementation Notes',
  'The ingestion pipeline extracts headings, paragraphs, tables, and code examples before embedding the document.',
  '',
  '```cpp',
  'std::string normalize_chunk(std::string_view input) { return trim(input); }',
  '```',
  '',
  '| field | value |',
  '| --- | --- |',
  '| source | hydra://knowledge-base |',
  '| format | markdown |',
  '',
  '[source](https://example.invalid/knowledge-base)\n',
].join('\n');

export function createAiMarkdownBlock(length, offset, salt) {
  const prefix = `${AI_MD_SECTION}\n<!-- document_offset=${offset}; salt=${salt} -->\n`;
  const template = Buffer.from(prefix, 'utf8');
  const block = Buffer.allocUnsafe(length);
  for (let position = 0; position < length; position += template.length) {
    template.copy(block, position, 0, Math.min(template.length, length - position));
  }
  return block;
}
