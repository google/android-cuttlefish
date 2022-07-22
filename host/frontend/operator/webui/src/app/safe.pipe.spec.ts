import { SafePipe } from "./safe.pipe";
import { BrowserModule, DomSanitizer } from "@angular/platform-browser";
import { TestBed } from "@angular/core/testing";

describe("SafePipe", () => {
  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [BrowserModule],
    });
  });

  it("create an instance", () => {
    const domSanitizer = TestBed.inject(DomSanitizer);
    const pipe = new SafePipe(domSanitizer);
    expect(pipe).toBeTruthy();
  });
});
