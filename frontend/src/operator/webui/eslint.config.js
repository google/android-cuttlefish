const gts = require('gts');
const {defineConfig} = require('eslint/config');

module.exports = defineConfig([
  {
    ignores: ['build/', 'dist/', '**/*.conf.js'],
  },
  ...gts,
]);
